#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>
#include <linux/pci.h>
#include<asm/uaccess.h>
#include<asm/io.h>
#include<asm/io.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>


void __iomem *regs_base_addr ;
resource_size_t regs_start;
resource_size_t regs_len;
void __iomem *data_base_addr;
resource_size_t data_mmio_start;
resource_size_t data_mmio_len;
struct pci_dev * pdev;
struct random
{
	char p;
	int id;
};




int __init initialize(void)
{
	
	pdev = pci_get_device(6900, 4368 , NULL);
	
	data_mmio_start = pci_resource_start(pdev, 2);
	data_mmio_len = pci_resource_len(pdev, 2);
	data_base_addr = pci_iomap_wc(pdev, 2, 0);
	
	regs_start = pci_resource_start(pdev, 0);
	regs_len = pci_resource_len(pdev, 0);
	regs_base_addr = pci_iomap(pdev, 0, 0x100);

	struct random *struct_transfer = kmalloc(sizeof(struct random), GFP_KERNEL); 
	struct_transfer->p = 'h';
	struct_transfer->id = 0;

	char* buffer = "Hello";
	//memcpy_toio(data_base_addr, struct_transfer, 5);
	void* buffer2 = kmalloc(sizeof(struct random), GFP_KERNEL);
	memcpy_fromio(buffer2, data_base_addr,sizeof(struct random));
	struct random *final = (struct random *)buffer2;
	printk(KERN_INFO "Reading %c", final->p);


	return 0;

}



static void __exit unload(void)
{
	pci_iounmap(pdev, regs_base_addr);
	pci_iounmap(pdev, data_base_addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	printk(KERN_INFO "Unloading Module");
}



module_init(initialize);
module_exit(unload);
MODULE_LICENSE("GPL");
