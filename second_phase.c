#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>
#include <linux/pci.h>
#include<asm/uaccess.h>
#include<asm/io.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/poll.h>

#define IVSHMEM_VENDOR_ID 0x1AF4
#define IVSHMEM_DEVICE_ID 0x1110
#define DRIVER_NAME "ivshmem"
#define VECTOR_ID 1 //used for trigger interrupt
#define NUM_VECTOR 2
#define DEST_ID 1


void __iomem *regs_base_addr ;
resource_size_t regs_start;
resource_size_t regs_len;
void __iomem *data_base_addr;
resource_size_t data_mmio_start;
resource_size_t data_mmio_len;

static int vectors[NUM_VECTOR];
static int irqs[NUM_VECTOR];
static int irq_flag = 0;







enum {
	/* KVM Inter-VM shared memory device register offsets */
	IntrMask        = 0x00,    /* Interrupt Mask */
	IntrStatus      = 0x04,    /* Interrupt Status */
	IVPosition      = 0x08,    /* VM ID */
	Doorbell        = 0x0c,    /* Doorbell */
};

irqreturn_t irq_handler(int irq, void *dev_id)
{
  	int msg;
  	msg = readl(data_base_addr);
  	printk(KERN_INFO "SHUANGDAO: irq_handler get called!, irq_number: %d \
  	msg received: 0x%x", irq, msg);
  	irq_flag = 1;

  	return IRQ_HANDLED;
}


int init_interrupt(struct pci_dev *pdev)
{
	int i, nvec, ret;
	/* set all masks to on*/
	writel(0xffffffff, regs_base_addr + IntrMask);

	nvec = pci_alloc_irq_vectors(pdev, NUM_VECTOR, NUM_VECTOR, PCI_IRQ_MSIX);
  	if (nvec < 0)
  	{
    	printk(KERN_ERR "Fail to allocate irq vectors %d", nvec);
    	goto out_release;
  	}

  	printk(KERN_DEBUG "Successfully allocate %d irq vectors", nvec);

  // get the irq numbers for each requet
  	for (i = 0; i < NUM_VECTOR; i++)
  	{
    	vectors[i] = i;
    	irqs[i] = pci_irq_vector(pdev, i);
    	printk(KERN_DEBUG "The irq number is %d for vector %d", irqs[i], i);

    	ret = request_irq(irqs[i], irq_handler, IRQF_SHARED, DRIVER_NAME, pdev);
    	if (ret) 
    	{
      		printk(KERN_ERR "Fail to request shared irq %d, error: %d", irqs[i], ret);
      		goto out_free_vec;
    	}
	}
	return 0;


	out_free_vec:
  		pci_free_irq_vectors(pdev);
	out_release:
  		pci_release_regions(pdev);

  	return -1;
}



int __init initialize(void)
{	

	
	//printk(KERN_INFO "1\n");	
	int p;	
	struct pci_dev *pdev = pci_get_device(IVSHMEM_VENDOR_ID, 
		IVSHMEM_DEVICE_ID , NULL);
	uint32_t msg;
	//printk(KERN_INFO "2\n");
	//char buf[] = KERN_INFO "Writing %s";
	//char* s= ""  ; 	
	

  // enable the PCI device
  	if (pci_enable_device(pdev))
    	return -ENODEV;
  	printk(KERN_DEBUG "Successfully enable the device\n");

  // request the region
	if (pci_request_regions(pdev, DRIVER_NAME))
    	goto out_disable;
	printk(KERN_DEBUG "Successfully reserve the resource\n");


	//printk(KERN_INFO "3\n");
	data_mmio_start = pci_resource_start(pdev, 2);
	data_mmio_len = pci_resource_len(pdev, 2);
	data_base_addr = pci_iomap(pdev, 2, 0);
	//printk(KERN_INFO "4\n");
	
	regs_start = pci_resource_start(pdev, 0);
	regs_len = pci_resource_len(pdev, 0);
	regs_base_addr = pci_iomap(pdev, 0, 0x100);
	
	p = init_interrupt(pdev);
	if(p<0)
	{
		return -1;
	}
	//printk(KERN_INFO "First 4 bytes: %08X\n", ioread32(data_base_addr));
	iowrite32(0xabcd0023, data_base_addr);
	

	msg = ((DEST_ID & 0xffff) << 16) + (VECTOR_ID & 0xffff);
    	printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
    	writel(msg, regs_base_addr + Doorbell);

	pci_iounmap(pdev, regs_base_addr);
	pci_iounmap(pdev, data_base_addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return 0;



	out_disable:
	  	pci_disable_device(pdev);
  	return -1;
}



static void __exit unload(void)
{
	printk(KERN_INFO "Unloading Module");
}



module_init(initialize);
module_exit(unload);
MODULE_LICENSE("GPL");


