#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>
#include <linux/pci.h>
#include<asm/uaccess.h>
#include<asm/io.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <popcorn/stat.h>
#include "ring_buffer.h"
#include "common.h"
enum {
	SEND_FLAG_POSTED = 0,
};


struct q_item {
	struct pcn_kmsg_message *msg;
	unsigned long flags;
	struct completion *done;
};
#define MAX_SEND_DEPTH	1024
#define NUMBER_OF_HANDLES 2

#define TEST_LAYER 1

void __iomem *regs_base_addr ;
resource_size_t regs_start;
resource_size_t regs_len;
void __iomem *data_base_addr;
resource_size_t data_mmio_start;
resource_size_t data_mmio_len;

/*each divided shared memory handler*/
struct ivshmem_handle
{
	int nid;
	struct q_item *msg_q;
	unsigned long q_head;
	unsigned long q_tail;
	spinlock_t q_lock;
	struct semaphore q_empty;
	struct semaphore q_full;


	void __iomem *data_base_addr_parted;
	struct task_struct *send_handler;
	struct task_struct *recv_handler;

};

static struct ivshmem_handle ivshmem_handles[NUMBER_OF_HANDLES] = {};
static struct ring_buffer send_buffer = {};


static int ivshmem_send(void __iomem *data_base_addr_parted,char* buf, size_t len)
{
	iowrite32(*buf, data_base_addr_parted);
	return 0;
}

static int ivshmem_recv(void __iomem *data_base_addr_parted, char *buf, size_t len)
{
	printk(KERN_INFO "First 4 bytes: %08X\n", ioread32(data_base_addr));
	return 0;
}


static int recv_handler(void* arg0)
{
	struct ivshmem_handle *ih = arg0;
	printk(KERN_INFO "RECV handler for %d is ready\n", ih->nid);

	while(!kthread_should_stop()){
		int len;
		int ret;
		size_t offset;
		struct pcn_kmsg_hdr header;
		char *data;

		offset = 0;
		len = sizeof(header);
		while (len > 0) {
			ret = ivshmem_recv(ih->data_base_addr_parted, (char *)(&header) + offset, len);
			if (ret == -1) break;
			offset += ret;
			len -= ret;
		}
		if (ret<0) break;


		data = kmalloc(header.size, GFP_KERNEL);
		BUG_ON(!data && "Unable to alloc a message");

		memcpy(data, &header, sizeof(header));

		offset = sizeof(header);
		len = header.size - offset;

		while (len > 0) {
			ret = ivshmem_recv(ih->data_base_addr_parted, data + offset, len);
			if (ret == -1) break;
			offset += ret;
			len -= ret;
		}
		if (ret < 0) break;

		/* Call pcn_kmsg upper layer */
		pcn_kmsg_process((struct pcn_kmsg_message *)data);



	}
	

}
	
	
	
static int enq_send(int dest_nid, struct pcn_kmsg_message *msg, unsigned long flags, struct completion *done)
{
	int ret;
	unsigned long at;
	struct ivshmem_handle *ih = ivshmem_handles + dest_nid;
	struct q_item *qi;
	do
	{
		ret= down_interruptible(&ih->q_full);
	}while(ret);

	spin_lock(&ih->q_lock);
	at = ih->q_tail;
	qi = ih->msg_q + at;
	ih->q_tail = (at + 1) & (MAX_SEND_DEPTH - 1);
	qi->msg = msg;
	qi->flags = flags;
	qi->done = done;
	spin_unlock(&ih->q_lock);
	up(&ih->q_empty);

	
	return at;
}

void ivshmem_kmsg_put(struct pcn_kmsg_message *msg);

static int deq_send(struct ivshmem_handle *ih)
{
	int ret;
	char *p;
	unsigned long from;
	size_t remaining;
	struct pcn_kmsg_message *msg;
	struct q_item *qi;
	unsigned long flags;
	struct completion *done;

	do {
		ret = down_interruptible(&ih->q_empty);
	} while (ret);

	spin_lock(&ih->q_lock);
	from = ih->q_head;
	qi = ih->msg_q + from;
	ih->q_head = (from + 1) & (MAX_SEND_DEPTH - 1);

	msg = qi->msg;
	flags = qi->flags;
	done = qi->done;
	spin_unlock(&ih->q_lock);
	up(&ih->q_full);

	p = (char *)msg;
	remaining = msg->header.size;

	while(remaining > 0)
	{
		int sent = ivshmem_send(ih->data_base_addr_parted, p, remaining);
		if(sent < 0)
		{
			printk(KERN_INFO "send interrupted, %d\n", sent);
			io_schedule();
			continue;
		}
		p += sent;
		remaining -= sent;

	}
	if(test_bit(SEND_FLAG_POSTED, &flags))
	{
		ivshmem_kmsg_put(msg);
	}

	if(done) complete(done);

	return 0;

}


static int send_handler(void* arg0)
{
	struct ivshmem_handle *ih = arg0;
	printk(KERN_INFO "SEND HANDLER for %d is ready\n", ih->nid);

	while(!kthread_should_stop()){
		deq_send(ih);
	}
	kfree(ih->msg_q);
	return 0;
}

struct pcn_kmsg_message *ivshmem_kmsg_get(size_t size)
{
	struct pcn_kmsg_message *msg;

	#ifdef TEST_LAYER
	msg = kmalloc(size, GFP_KERNEL);
	#else
	while(!(msg = ring_buffer_get(&send_buffer, size)))
	{nid
		WARN_ON_ONCE("ring buffer is fill");
		schedule();
	}
	#endif
	return msg;
}

int ivshmem_kmsg_send(int dest_nid, struct pcn_kmsg_message *msg, size_t size)
{
	DECLARE_COMPLETION_ONSTACK(done);
	enq_send(dest_nid, msg,0 ,&done);
	return 0;
}

void ivshmem_kmsg_put(struct pcn_kmsg_message *msg)
{
#ifdef TEST_LAYER
	kfree(msg);
#else
	ring_buffer_put(&send_buffer, msg);
#endif
}


int ivshmem_kmsg_post(int dest_nid, struct pcn_kmsg_message *msg, size_t size)
{
	enq_send(dest_nid, msg,  1 << SEND_FLAG_POSTED, NULL);
	return 0;
}

void ivshmem_kmsg_done(struct pcn_kmsg_message *msg)
{
	kfree(msg);
}

struct pcn_kmsg_transport transport_socket = {
	.name = "ivshmem",
	.features = 0,

	.get = ivshmem_kmsg_get,
	.put = ivshmem_kmsg_put,
	.send = ivshmem_kmsg_send,
	.post = ivshmem_kmsg_post,
	.done = ivshmem_kmsg_done,
};


static struct task_struct * __init __start_handler(const int nid, const char *type, int (*handler)(void *data))
{
	char name[40];
	struct task_struct *tsk;

	sprintf(name, "pcn_%s_%d", type, nid);
	if(type == "send")
	{
		tsk = kthread_run(handler, ivshmem_handles + nid, name);
	}
	else
	{
		tsk = kthread_run(handler, ivshmem_handles + 1 - nid, name);
	}
	if (IS_ERR(tsk)) {
		printk(KERN_ERR "Cannot create %s handler, %ld\n", name, PTR_ERR(tsk));
		return tsk;
	}

	return tsk;
}

static int __start_handlers(const int nid)
{
	struct task_struct *tsk_send, *tsk_recv;
	tsk_send = __start_handler(nid, "send", send_handler);
	if (IS_ERR(tsk_send)) {
		return PTR_ERR(tsk_send);
	}

	tsk_recv = __start_handler(nid, "recv", recv_handler);
	if (IS_ERR(tsk_recv)) {
		kthread_stop(tsk_send);
		return PTR_ERR(tsk_recv);
	}
	ivshmem_handles[nid].send_handler = tsk_send;
	ivshmem_handles[nid].recv_handler = tsk_recv;
	return 0;
}


static void __exit unload(void)
{
	int i;
	for (i = 0; i < MAX_NUM_NODES; i++) {
		struct ivshmem_handle *sh = ivshmem_handles + i;
		if (sh->send_handler) {
			kthread_stop(sh->send_handler);
		} else {
			if (sh->msg_q) kfree(sh->msg_q);
		}
		if (sh->recv_handler) {
			kthread_stop(sh->recv_handler);
		}
	}		
	ring_buffer_destroy(&send_buffer);

	printk(KERN_INFO "Unloading Module");
}




int __init initialize(void)
{
	int ret,i;
	struct pci_dev *pdev = pci_get_device(6900, 4368 , NULL);
	

	data_mmio_start = pci_resource_start(pdev, 2);
	data_mmio_len = pci_resource_len(pdev, 2);
	data_base_addr = pci_iomap(pdev, 2, 0);
	
	regs_start = pci_resource_start(pdev, 0);
	regs_len = pci_resource_len(pdev, 0);
	regs_base_addr = pci_iomap(pdev, 0, 0x100);
	//iowrite32(0xabcd0023, data_base_addr);

	printk(KERN_INFO "Loading Popcorn messaging layer over IVSHMEM...\n");
	
	//pcn_kmsg_set_transport(&transport_socket);

	for (i = 0; i < NUMBER_OF_HANDLES; i++) {
		struct ivshmem_handle *ih = ivshmem_handles + i;

		ih->msg_q = kmalloc(sizeof(*ih->msg_q) * MAX_SEND_DEPTH, GFP_KERNEL);
		if (!ih->msg_q) {
			ret = -ENOMEM;
			goto out_exit;
		}

		ih->nid = i;
		ih->q_head = 0;
		ih->q_tail = 0;
		spin_lock_init(&ih->q_lock);
		ih->data_base_addr_parted = data_base_addr + data_mmio_len*i/NUMBER_OF_HANDLES;

		sema_init(&ih->q_empty, 0);
		sema_init(&ih->q_full, MAX_SEND_DEPTH);
	}


	if ((ret = ring_buffer_init(&send_buffer, "ivshmem_send"))) goto out_exit;

	set_popcorn_node_online(my_nid,true);

	printk(KERN_INFO "Ready on IVSHMEM\n");

	pci_iounmap(pdev, regs_base_addr);
	pci_iounmap(pdev, data_base_addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return 0;

out_exit:
	return ret;
}





module_init(initialize);
module_exit(unload);
MODULE_LICENSE("GPL");
