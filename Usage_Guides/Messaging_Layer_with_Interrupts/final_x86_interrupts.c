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

	#define NUMBER_OF_HANDLES 2
	#define DEST_ID 1

	struct q_item {
		struct pcn_kmsg_message *msg;
		unsigned long flags;
		struct completion *done;
	};

	int* VM1;
	int* VM2;
	int* OPEN;



	#define MSG_LENGTH 16
	#define SEG_SIZE 20// to support max msg size 65536

	// spinlock_t lock[NUMBER_OF_HANDLES];

	struct pci_dev *pdev;
	int my_nid  = 0;

	struct test_msg_t {
		struct pcn_kmsg_hdr header;
		unsigned char payload[MSG_LENGTH];
	};

	#define MAX_SEND_DEPTH	10

	#define TEST_LAYER 1
	#define MSG_LENGTH 16
	//#define MSG_LENGTH 16384
	//#define MSG_LENGTH 4096
	#define NUM_MSGS 10

	void __iomem *regs_base_addr ;
	resource_size_t regs_start;
	resource_size_t regs_len;
	void __iomem *data_base_addr;
	resource_size_t data_mmio_start;
	resource_size_t data_mmio_len;


	/*each divided shared memory handler*/
	struct ivshmem_handle
	{
		int id;
		struct q_item *msg_q;
		int q_size;
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


	spinlock_t x86_lock;

	char *msg_names[] = {
		"TEST",
		"TEST_LONG",
		"CHECKIN",
		"MCAST",
		"PROC_SRV_CLONE_REQUEST",
		"PROC_SRV_CREATE_PROCESS_PAIRING",
		"PROC_SRV_EXIT_PROCESS",
		"PROC_SRV_BACK_MIG_REQUEST",
		"PROC_SRV_VMA_OP",
		"PROC_SRV_VMA_LOCK",
		"PROC_SRV_MAPPING_REQUEST",
		"PROC_SRV_NEW_KERNEL",
		"PROC_SRV_NEW_KERNEL_ANSWER",
		"PROC_SRV_MAPPING_RESPONSE",
		"PROC_SRV_MAPPING_RESPONSE_VOID",
		"PROC_SRV_INVALID_DATA",
		"PROC_SRV_ACK_DATA",
		"PROC_SRV_THREAD_COUNT_REQUEST",
		"PROC_SRV_THREAD_COUNT_RESPONSE",
		"PROC_SRV_THREAD_GROUP_EXITED_NOTIFICATION",
		"PROC_SRV_VMA_ACK",
		"PROC_SRV_BACK_MIGRATION",
		"PCN_PERF_START_MESSAGE",
		"PCN_PERF_END_MESSAGE",
		"PCN_PERF_CONTEXT_MESSAGE",
		"PCN_PERF_ENTRY_MESSAGE",
		"PCN_PERF_END_ACK_MESSAGE",
		"START_TEST",
		"REQUEST_TEST",
		"ANSWER_TEST",
		"MCAST_CLOSE",
		"SHMTUN",
		"REMOTE_PROC_MEMINFO_REQUEST",
		"REMOTE_PROC_MEMINFO_RESPONSE",
		"REMOTE_PROC_STAT_REQUEST",
		"REMOTE_PROC_STAT_RESPONSE",
		"REMOTE_PID_REQUEST",
		"REMOTE_PID_RESPONSE",
		"REMOTE_PID_STAT_REQUEST",
		"REMOTE_PID_STAT_RESPONSE",
		"REMOTE_PID_CPUSET_REQUEST",
		"REMOTE_PID_CPUSET_RESPONSE",
		"REMOTE_SENDSIG_REQUEST",
		"REMOTE_SENDSIG_RESPONSE",
		"REMOTE_SENDSIGPROCMASK_REQUEST",
		"REMOTE_SENDSIGPROCMASK_RESPONSE",
		"REMOTE_SENDSIGACTION_REQUEST",
		"REMOTE_SENDSIGACTION_RESPONSE",
		"REMOTE_IPC_SEMGET_REQUEST",
		"REMOTE_IPC_SEMGET_RESPONSE",
		"REMOTE_IPC_SEMCTL_REQUEST",
		"REMOTE_IPC_SEMCTL_RESPONSE",
		"REMOTE_IPC_SHMGET_REQUEST",
		"REMOTE_IPC_SHMGET_RESPONSE",
		"REMOTE_IPC_SHMAT_REQUEST",
		"REMOTE_IPC_SHMAT_RESPONSE",
		"REMOTE_IPC_FUTEX_WAKE_REQUEST",
		"REMOTE_IPC_FUTEX_WAKE_RESPONSE",
		"REMOTE_PFN_REQUEST",
		"REMOTE_PFN_RESPONSE",
		"REMOTE_IPC_FUTEX_KEY_REQUEST",
		"REMOTE_IPC_FUTEX_KEY_RESPONSE",
		"REMOTE_IPC_FUTEX_TOKEN_REQUEST",
		"REMOTE_PROC_CPUINFO_RESPONSE",
		"REMOTE_PROC_CPUINFO_REQUEST",
		"PROC_SRV_CREATE_THREAD_PULL",
		"PCN_KMSG_TERMINATE",
		"SELFIE_TEST",
		"FILE_MIGRATE_REQUEST",
		"FILE_OPEN_REQUEST",
		"FILE_OPEN_REPLY",
		"FILE_STATUS_REQUEST",
		"FILE_STATUS_REPLY",
		"FILE_OFFSET_REQUEST",
		"FILE_OFFSET_REPLY",
		"FILE_CLOSE_NOTIFICATION",
		"FILE_OFFSET_UPDATE",
		"FILE_OFFSET_CONFIRM",
		"FILE_LSEEK_NOTIFICATION",
		"SCHED_PERIODIC"
	};


	pcn_kmsg_cbftn callbacks[PCN_KMSG_TYPE_MAX];

	int find_low_queue(void)
	{
		int min = 0;
		int mincount = 10000;
		int i;
		for( i=0; i<NUMBER_OF_HANDLES ; i++)
		{
			struct ivshmem_handle *ih = ivshmem_handles + i;
			if(ih->q_size  == 0)
			{
				return i;
			}
			if(ih->q_size < mincount)
			{
				mincount = ih->q_size;
				min = i;
			}

		}
		return min;
	}


	int pcn_kmsg_register_callback(enum pcn_kmsg_type type, pcn_kmsg_cbftn callback)
	{
		if (type >= PCN_KMSG_TYPE_SELFIE_TEST)
			return -EINVAL;

		printk(KERN_INFO "%s: registering %d \n", __func__, type);
		callbacks[type] = callback;
		return 0;
	}

	int pcn_kmsg_unregister_callback(enum pcn_kmsg_type type)
	{
		if (type >= PCN_KMSG_TYPE_SELFIE_TEST)
			return -EINVAL;

		printk(KERN_INFO "Unregistering callback %d\n", type);
		callbacks[type] = NULL;
		return 0;
	}

	static int ivshmem_send(void __iomem *data_base_addr_parted ,struct pcn_kmsg_message* buf, int handles_id)
	{

		// printk(KERN_INFO "Function Starts: %s", __func__);


		printk(KERN_INFO "Started Writing Message to Shared Memory from x86 | handles: %d\n", handles_id);

		// spin_lock(&lock[handles_id]);
		memcpy_toio(data_base_addr_parted, buf, sizeof(struct pcn_kmsg_message));
		// spin_unlock(&lock[handles_id]);

		spin_lock(&x86_lock);

		msg = ((DEST_ID & 0xffff) << 16) + (1 & 0xffff);
	    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	    iowrite32(msg, regs_base_addr + Doorbell);



		printk(KERN_INFO "Done Writing Message to Shared Memory from x86 | handles: %d\n", handles_id);
		return sizeof(struct pcn_kmsg_message);

	}

	static int ivshmem_recv(void __iomem *data_base_addr_parted ,void* buf, int handles_id)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		printk(KERN_INFO "Started Reading Message from Shared Memory from x86 | handles: %d\n", handles_id);
		// spin_lock(&lock[handles_id]);
		memcpy_fromio(buf, data_base_addr_parted, sizeof(struct pcn_kmsg_message));
		// spin_unlock(&lock[handles_id]);
		printk(KERN_INFO "Done Reading Message from Shared Memory from x86 | handles: %d\n", handles_id);
		
		return sizeof(struct pcn_kmsg_message);
	}

	/*Interrupt Handlers*/



	/*
		Interrupt Vector:
	*	read_x86: 0
	*	read_arm: 1
	*	write_x86: 2
	*	write_arm: 3
	*/
	static irqreturn_t read_x86(int irq, void* dev_instance)
	{
		int ret;
	    ret = recv_handler(ivshmem_handles); 

	    if(ret < 0)
	    	return IRQ_NONE;

		msg = ((DEST_ID & 0xffff) << 16) + (3 & 0xffff);
	    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	    iowrite32(msg, regs_base_addr + Doorbell);

		

		return IRQ_HANDLED;


	}

	static irqreturn_t read_arm(int irq, void* dev_instance)
	{

		

		int ret;
	    ret = recv_handler(ivshmem_handles + 1); 

	    if(ret < 0)
	    	return IRQ_NONE;

		msg = ((DEST_ID & 0xffff) << 16) + (2 & 0xffff);
	    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	    iowrite32(msg, regs_base_addr + Doorbell);



		

		return IRQ_HANDLED;


	}

	static irqreturn_t write_x86(int irq, void* dev_instance)
	{

		// send message
		spin_unlock(&x86_lock);


		return IRQ_HANDLED;


	}

	static irqreturn_t write_arm(int irq, void* dev_instance)
	{

		// send message

		// spin_unlock(&arm_lock);

		return IRQ_HANDLED;


	}


	/*Interrupt Handler End*/

	static int recv_handler(void* arg0)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		struct ivshmem_handle *ih = arg0;
		// printk(KERN_INFO "RECV handler for %d is ready\n", ih->id);

			int len;
			int ret;
			size_t offset;
			void* input = kmallwrite_startwrite_startoc(sizeof(struct pcn_kmsg_message), GFP_KERNEL);
			struct pcn_kmsg_message *header;
			void *data;

			offset = 0;
			// printk(KERN_INFO "Point 1\n");
			// printk(KERN_INFO " %d", offset);
			len = sizeof(header);
			// while (len > 0) {
				ret = ivshmem_recv(ih->data_base_addr_parted, input,ih->id);
				header = (struct pcn_kmsg_message *)input;
				if (ret == -1) break;
				offset += ret;
				len -= ret;
				schedule();
			// }
			// if (ret<0) break;
			// printk(KERN_INFO "Point 2\n");



			// data = kmalloc(sizeof(header), GFP_KERNEL);
			// BUG_ON(!data && "Unable to alloc a message");

			// memcpy(data, &header, sizeof(header));
			// printk(KERN_INFO "Point 3\n");

			// offset = sizeof(header);
			// len = sizeof(header) - offset;

			// // while (len > 0) {
			// 	ret = ivshmem_recvheader(ih->data_base_addr_parted, data);
			// 	if (ret == -1) break;
			// 	offset += ret;
			// 	len -= ret;
			// // }
			// if (ret < 0) break;
			// printk(KERN_INFO "Point 4\n");

			/* Call pcn_kmsg upper layer */
			// pcn_kmsg_process(header);

			printk(KERN_INFO "Message: %d", header->header.type);
			kfree(input);

		
		return 0;
	}

		

	static int enq_send(int dest_nid, struct pcn_kmsg_message *msg, unsigned long flags, struct completion *done)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		int ret;
		unsigned long at;
		struct ivshmem_handle *ih = ivshmem_handles + find_low_queue();
		struct q_item *qi ;
		do
		{
			ret= down_interruptible(&ih->q_full);
		}while(ret);

		// printk(KERN_INFO "1\n");
		// printk(KERN_INFO "ih nid(enq_send): %d, at: %d", ih->nid, ih->q_tail);
		spin_lock(&ih->q_lock);
		// printk(KERN_INFO "2\n");
		at = ih->q_tail;
		// printk(KERN_INFO "3\n");
		qi = (ih->msg_q + at);
		// printk(KERN_INFO "4\n");
		ih->q_tail = (at + 1) & (MAX_SEND_DEPTH - 1);
		// printk(KERN_INFO "51\n");
		// printk(KERN_INFO "sizeof qi->msg: %d; sizeof msg: %d\n",sizeof(qi->msg),sizeof(msg));
		// printk(KERN_INFO "52\n");
		// printk(KERN_INFO "1: %p", ih->msg_q);
		// printk(KERN_INFO "2: %p", msg);
		// printk(KERN_INFO "3: %p", ih->msg_q + at);
		qi->msg = msg;
		// printk(KERN_INFO "6\n");
		qi->flags = flags;
		// printk(KERN_INFO "7\n");
		qi->done = done;
		ih->q_size++;
		// printk(KERN_INFO "8\n");
		// printk(KERN_INFO "enq_send msg_q: %p\n", ih->msg_q);
		spin_unlock(&ih->q_lock);
		 up(&ih->q_empty);

		
		return at;
	}

	void ivshmem_kmsg_put(struct pcn_kmsg_message *msg);

	static int deq_send(struct ivshmem_handle *ih)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		int ret;
		unsigned long from;
		//size_t remaining;
		struct pcn_kmsg_message *msg;
		struct q_item *qi;
		unsigned long flags;
		struct completion *done;
		int sent;
		// printk(KERN_INFO "Starting down_interruptible");

		do {
			ret = down_interruptible(&ih->q_empty);
		} while (ret);

		// printk(KERN_INFO "Started Spinlock");
		spin_lock(&ih->q_lock);
		// printk(KERN_INFO "Crossed Spinlock");
		from = ih->q_head;

		// printk(KERN_INFO "1\n");
		// printk(KERN_INFO "1: %p\n", ih->msg_q);
		// printk(KERN_INFO "from valubachchane: %d\n", from);
		// printk(KERN_INFO "3: %p\n", ih->msg_q + from);
		qi = ih->msg_q + from;
		// printk(KERN_INFO "2\n");
		ih->q_head = (from + 1) & (MAX_SEND_DEPTH - 1);
		// printk(KERN_INFO "3\n");
		// printk(KERN_INFO "ih id(deq_send): %d\n", ih->id);
		// printk(KERN_INFO "1: %p\n", qi);
		// printk(KERN_INFO "2: %p\n", msg);
		// printk(KERN_INFO "3: %p\n", qi->msg);
		msg = qi->msg;
		// printk(KERN_INFO "In the middle");
		flags = qi->flags;
		done = qi->done;
		spin_unlock(&ih->q_lock);
		up(&ih->q_full);
		// printk(KERN_INFO "Ended Spinlock");

		// p = (char *)msg;
		// remaining = msg->header.size;

		while(ih->q_size > 0)
		 {
			// printk(KERN_INFO "Started Sending Message\n");
			sent = ivshmem_send(ih->data_base_addr_parted, msg, ih->id);
			if(sent < 0)
			{
				printk(KERN_INFO "send interrupted, %d\n", sent);
				io_schedule();
				//continue;
			}
			ih->q_size--;
			// printk(KERN_INFO "Compete Sending Message\n");
			// p += sent;
			// remaining -= sent;

		}
		// if(test_bit(SEND_FLAG_POSTED, &flags))
		// {
		// 	ivshmem_kmsg_put(msg);
		// }

		// if(done) complete(done);

		// printk(KERN_INFO "hello\n");

		return 0;

	}


	static int send_handler(void* arg0)
	{
		struct ivshmem_handle *ih = arg0;
		// printk(KERN_INFO "Function Starts: %s", __func__);
		// printk(KERN_INFO "SEND HANDLER for %d is ready\n", ih->id);

		// printk(KERN_INFO "send_handler msg_q: %p\n", ih->msg_q);
		while(!kthread_should_stop()){
			deq_send(ih);
			schedule();
		}
		kfree(ih->msg_q);
		return 0;
	}

	struct pcn_kmsg_message *ivshmem_kmsg_get(size_t size)
	{
		struct pcn_kmsg_message *msg;
		// printk(KERN_INFO "Function Starts: %s", __func__);

		#ifdef TEST_LAYER
		msg = kmalloc(size, GFP_KERNEL);
		#else
		while(!(msg = ring_buffer_get(&send_buffer, size)))
		{
			WARN_ON_ONCE("ring buffer is full");
			schedule();
		}
		#endif
		return msg;
	}

	int ivshmem_kmsg_send(int dest_nid, struct pcn_kmsg_message *msg, size_t size)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		DECLARE_COMPLETION_ONSTACK(done);
		enq_send(dest_nid, msg,0 ,&done);
		return 0;
	}

	void ivshmem_kmsg_put(struct pcn_kmsg_message *msg)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
	#ifdef TEST_LAYER
		kfree(msg);
	#else
		ring_buffer_put(&send_buffer, msg);
	#endif
	}


	int ivshmem_kmsg_post(int dest_nid, struct pcn_kmsg_message *msg, size_t size)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		enq_send(dest_nid, msg,  1 << SEND_FLAG_POSTED, NULL);
		return 0;
	}

	void ivshmem_kmsg_done(struct pcn_kmsg_message *msg)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
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


	static struct task_struct * __init __start_handler(const int handles_nid, const char *type, int (*handler)(void *data))
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		char name[40];
		struct task_struct *tsk;

		sprintf(name, "pcn_%s_%d", type, handles_nid);
		tsk = kthread_run(handler, ivshmem_handles + handles_nid, name);
		if (IS_ERR(tsk)) {
			printk(KERN_ERR "Cannot create %s handler, %ld\n", name, PTR_ERR(tsk));
			return tsk;
		}

		return tsk;
	}

	static int __start_handlers(const int handles_nid)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);


		struct task_struct *tsk_send, *tsk_recv;
		tsk_send = __start_handler(handles_nid, "send", send_handler);
		if (IS_ERR(tsk_send)) {
			return PTR_ERR(tsk_send);
		}

		// tsk_recv = __start_handler(handles_nid, "recv", recv_handler);
		// if (IS_ERR(tsk_recv)) {
		// 	kthread_stop(tsk_send);
		// 	return PTR_ERR(tsk_recv);
		// }


		// printk("__start_handlers ih->msg_q %p", ivshmem_handles[handles_nid].msg_q);
		ivshmem_handles[handles_nid].send_handler = tsk_send;
		// ivshmem_handles[handles_nid].recv_handler = tsk_recv;

		return 0;
	}


	static void __exit unload(void)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		int i;
		for (i = 0; i < NUMBER_OF_HANDLES; i++) {
			struct ivshmem_handle *sh = ivshmem_handles + i;
			if (sh->send_handler) {
				kthread_stop(sh->send_handler);
			} else {
				if (sh->msg_q) kfree(sh->msg_q);
			}
			// if (sh->recv_handler) {
			// 	kthread_stop(sh->recv_handler);
			// }
		}
		kfree(VM1);
		kfree(VM2);
		kfree(OPEN);

		pci_iounmap(pdev, regs_base_addr);
		pci_iounmap(pdev, data_base_addr);
		pci_release_regions(pdev);
		pci_disable_device(pdev);	
		ring_buffer_destroy(&send_buffer);

		printk(KERN_INFO "Unloading Module");
	}

	/*checker messaging layer*/


	static void handle_selfie_test(struct pcn_kmsg_message *inc_msg)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		//int payload_size = MSG_LENGTH;
		//pci_kmsg_send_long(1, (struct pchandle_selfie_testn_kmsg_message *)inc_msg,payload_size);
		printk(KERN_INFO "Selfie Message Test Complete");
		pcn_kmsg_done(inc_msg);
	}
	int pci_kmsg_send_long(unsigned int dest_cpu, struct pcn_kmsg_message *lmsg, unsigned int payload_size)
	{
		// printk(KERN_INFO "Function Starts: %s", __func__);
		struct pcn_kmsg_message *pcn_msg = NULL;
		//pcn_kmsg_cbftn ftn;



		lmsg->header.from_nid = my_nid;
		lmsg->header.size = payload_size;

		if (lmsg->header.size > SEG_SIZE) {
			printk(KERN_ALERT"%s: ALERT: trying to send a message bigger than the supported size %d (%pS) %s\n",
			       __func__, (int)SEG_SIZE, __builtin_return_address(0),
			       msg_names[lmsg->header.type]);
		}


		// if (dest_cpu == my_nid) {
		// 	pcn_msg = lmsg;

		// 	printk(KERN_INFO "%s: INFO: Send message: dest_cpu == my_nid\n", __func__);

		// 	if (pcn_msg->header.type < 0
		// 	    || pcn_msg->header.type >= PCN_KMSG_TYPE_MAX) {
		// 		printk(KERN_ERR "Received invalid message type %d\n",
		// 		       pcn_msg->header.type);
		// 		pcn_kmsg_done(pcn_msg);
		// 	} else {
		// 		ftn = (pcn_kmsg_cbftn) callbacks[pcn_msg->header.type];
		// 		if (ftn != NULL) {
		// 			ftn((struct pcn_kmsg_message *)pcn_msg);
		// 		} else {
		// 			pcn_kmsg_done(pcn_msg);
		// 		}
		// 	}
		// 	return 0;
		// }

		pcn_msg = lmsg;
		ivshmem_kmsg_send(0, pcn_msg, sizeof(struct pcn_kmsg_message));

		/*send pcn_msg from here to shared memory*/




		return 0;
	}

	void test_func(void)
	{

		int i;
		// printk(KERN_INFO "Function Starts: %s", __func__);

		printk(KERN_INFO "Test function %s: called\n", __func__);

		pcn_kmsg_register_callback(PCN_KMSG_TYPE_SELFIE_TEST,
			(pcn_kmsg_cbftn)handle_selfie_test);

		printk(KERN_INFO "TYPE: %d", PCN_KMSG_TYPE_SELFIE_TEST);


		struct test_msg_t *msg;
		int payload_size = MSG_LENGTH;

		msg = (struct test_msg_t *) kmalloc(sizeof(struct test_msg_t), GFP_KERNEL);
		msg->header.type = PCN_KMSG_TYPE_SELFIE_TEST;
		memset(msg->payload, 'b', payload_size);


		for (i = 0; i < NUM_MSGS; i++) {
			pci_kmsg_send_long(1, (struct pcn_kmsg_message*)msg,
					   payload_size);

			// if (!(i%(NUM_MSGS/5))) {
			// 	printk(KERN_DEBUG "scheduling out\n");
			// 	msleep(1);
			// }
		}

		kfree(msg);
		printk(KERN_INFO "Finished Testing\n");

	}





	/*end checker messaging layer*/


	int __init initialize(void)
	{
		int ret,i;
		pdev = pci_get_device(6900, 4368 , NULL);
		// printk(KERN_INFO "Function Starts: %s", __func__);


		VM1= kmalloc(sizeof(int), GFP_KERNEL);
		VM2= kmalloc(sizeof(int), GFP_KERNEL);
		OPEN= kmalloc(sizeof(int), GFP_KERNEL);
		*VM1 =0;
		*VM2 =1;
		*OPEN=2;
		int p;
		for(p= 0 ; p<NUMBER_OF_HANDLES ; p++)
		{
			// spin_lock_init(&lock[p]);
		}
		data_mmio_start = pci_resource_start(pdev, 2);
		data_mmio_len = pci_resource_len(pdev, 2);
		data_base_addr = pci_iomap(pdev, 2, 0);
		
		regs_start = pci_resource_start(pdev, 0);
		regs_len = pci_resource_len(pdev, 0);	
		regs_base_addr = pci_iomap(pdev, 0, 0x100);

		printk(KERN_INFO "Loading Popcorn messaging layer over IVSHMEM...\n");
		// printk(KERN_INFO "First 4 bytes: %08X\n", ioread32(data_base_addr));
		
		pcn_kmsg_set_transport(&transport_socket);

		for (i = 0; i < NUMBER_OF_HANDLES; i++) 
		{
			struct ivshmem_handle *sh = ivshmem_handles + i;

			sh->msg_q = kmalloc(sizeof(*sh->msg_q) * MAX_SEND_DEPTH, GFP_KERNEL);
			if (!sh->msg_q) {
				ret = -ENOMEM;
				goto out_exit;
			}

			// printk("%d ivshmem_handle: %p", i , sh->msg_q);
			sh->id = i;
			sh->q_head = 0;
			sh->q_tail = 0;
			spin_lock_init(&sh->q_lock);
			sh->q_size = 0;
			sh->data_base_addr_parted = data_base_addr + (data_mmio_len*i)/NUMBER_OF_HANDLES + sizeof(int);
			
			sema_init(&sh->q_empty, 0);
			sema_init(&sh->q_full, MAX_SEND_DEPTH);
			// spin_lock(&lock[i]);
			memcpy_toio(sh->data_base_addr_parted - sizeof(int), OPEN, sizeof(int));
			// spin_unlock(&lock[i]);
		}		
		test_func();



		if ((ret = ring_buffer_init(&send_buffer, "ivshmem_send"))) goto out_exit;

		//set_popcorn_node_online(my_nid,true);

		// printk(KERN_INFO "Ivshmem msg_q %p" , ivshmem_handles[0].msg_q);

		// printk(KERN_INFO "my_nid: %d", my_nid);

		int q = 0;
		while(q<NUMBER_OF_HANDLES)
		{	
			ret = __start_handlers(q);
			if(ret) return ret;
			q++;
		}


		printk(KERN_INFO "Ready on IVSHMEM\n");

		

		return 0;

	out_exit:
		return ret;
	}





	module_init(initialize);
	module_exit(unload);
	MODULE_LICENSE("GPL");
