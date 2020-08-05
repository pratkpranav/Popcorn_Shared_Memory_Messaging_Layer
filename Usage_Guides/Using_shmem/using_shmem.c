#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>
#include <linux/pci.h>
#include<asm/uaccess.h>
#include<asm/io.h>

void __iomem *regs_base_addr ;
resource_size_t regs_start;
resource_size_t regs_len;
void __iomem *data_base_addr;
resource_size_t data_mmio_start;
resource_size_t data_mmio_len;
struct pci_dev *pdev;


int __init initialize(void)
{
	
	pdev = pci_get_device(6900, 4368 , NULL);
	
	data_mmio_start = pci_resource_start(pdev, 2);
	data_mmio_len = pci_resource_len(pdev, 2);
	data_base_addr = pci_iomap(pdev, 2, 0);
	
	regs_start = pci_resource_start(pdev, 0);
	regs_len = pci_resource_len(pdev, 0);
	regs_base_addr = pci_iomap(pdev, 0, 0x100);
	//printk(KERN_INFO "First 4 bytes: %08X\n", ioread32(data_base_addr));
	//iowrite32(0xabcd0023, data_base_addr);

	//pci_iounmap(pdev, regs_base_addr);
	//pci_iounmap(pdev, data_base_addr);
	//pci_release_regions(pdev);
	//pci_disable_device(pdev);
	return 0;

}



static void __exit unload(void)
{
	pci_iounmap(pdev, regs_base_addr);
	pci_iounmap(pdev, data_base_addr);
	pci_release_regions(pdev);
	printk(KERN_INFO "Unloading Module");
}



module_init(initialize);
module_exit(unload);
MODULE_LICENSE("GPL");
