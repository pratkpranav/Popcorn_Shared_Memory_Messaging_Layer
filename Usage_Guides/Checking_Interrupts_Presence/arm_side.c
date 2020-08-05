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
	#define DRIVER_NAME "ivshmem-doorbell"
	#define VECTOR_ID 0 //used for trigger interrupt
	#define NUM_VECTOR 2
	#define DEST_ID 0


	void __iomem *regs_base_addr ;
	resource_size_t regs_start;
	resource_size_t regs_len;
	void __iomem *data_base_addr;
	resource_size_t data_mmio_start;
	resource_size_t data_mmio_len;
	int nvectors;
	char (*msix_names)[256];
	struct msix_entry *msix_entries;


	struct pci_dev *pdev;


	enum {
		/* KVM Inter-VM shared memory device register offsets */
		IntrMask        = 0x00,    /* Interrupt Mask */
		IntrStatus      = 0x04,    /* Interrupt Status */
		IVPosition      = 0x08,    /* VM ID */
		Doorbell        = 0x0c,    /* Doorbell */
	};


	//enum interrupt {start_reading, end_reading, start_wriiting, end_writing};

	static irqreturn_t interrupt_handler_0(int irq, void* dev_instance)
	{
		u32 status;
		status = ioread32(regs_base_addr + Doorbell);
		printk(KERN_INFO "Reached Interrupt_handler 0");
		printk(KERN_INFO "IVSHMEM: interrupt (status = 0x%04x)\n",
			   status);
		// if(!status || (status == 0xFFFFFFFF))
		// 	return IRQ_NONE;

		if(status == 0)
		{
			int msg;
	  		msg = readl(data_base_addr);
	  		printk(KERN_INFO "1. IVSHMEM: irq_handler get called!, irq_number: %d \
	  		msg received: 0x%x", irq, msg);
	  	
		}
		
		printk(KERN_INFO "IVSHMEM: interrupt (status = 0x%04x)\n",
			   status);

		return IRQ_HANDLED;


	}


	static irqreturn_t interrupt_handler_1(int irq, void* dev_instance)
	{
		u32 status;
		status = ioread32(regs_base_addr + Doorbell);
		printk(KERN_INFO "Reached Interrupt_handler 1");
		printk(KERN_INFO "IVSHMEM: interrupt (status = 0x%04x)\n",
			   status);
		// if(!status || (status == 0xFFFFFFFF))
		// 	return IRQ_NONE;

		if(status == 0)
		{
			int msg;
	  		msg = readl(data_base_addr);
	  		printk(KERN_INFO "1. IVSHMEM: irq_handler get called!, irq_number: %d \
	  		msg received: 0x%x", irq, msg);
	  	
		}
		
		printk(KERN_INFO "IVSHMEM: interrupt (status = 0x%04x)\n",
			   status);

		return IRQ_HANDLED;


	}




	int init_interrupt(struct pci_dev *pdev, int nvectors)
	{
		int i, err;
		
		// nvectors = nvectors;
		msix_entries = kmalloc(nvectors * sizeof *msix_entries,
						   GFP_KERNEL);
		msix_names = kmalloc(nvectors * sizeof *msix_names,
						 GFP_KERNEL);

		for (i = 0; i < nvectors; ++i)
			msix_entries[i].entry = i;

		err = pci_enable_msix(pdev, msix_entries, nvectors);
		if (err > 0) {
			printk(KERN_INFO "no MSI. Back to INTx.\n");
			return -ENOSPC;
		}

		if (err) {
			printk(KERN_INFO "some error below zero %d\n", err);
			return err;
		}

		for (i = 0; i < nvectors; i++) {

			snprintf(msix_names[i], sizeof *msix_names,
			 "%s-config", DRIVER_NAME);

			if(i%2 == 0)
			{
				err = request_irq(msix_entries[i].vector,
					  interrupt_handler_0, IRQF_SHARED,
					  	msix_names[i], pdev);
			}
			else
			{
				err = request_irq(msix_entries[i].vector,
					  interrupt_handler_1, 0,
					  	msix_names[i], pdev);

			}

			if (err) {
				printk(KERN_INFO "couldn't allocate irq for msi-x entry %d with vector %d\n", i, msix_entries[i].vector);
				return -ENOSPC;
			}
		}
		printk(KERN_INFO "MSI-X allocation done");
		return 0;

	}



	int __init initialize(void)
	{	

		
		//printk(KERN_INFO "1\n");	
		int p;	
		pdev = pci_get_device(IVSHMEM_VENDOR_ID, 
			IVSHMEM_DEVICE_ID , NULL);
		uint32_t msg;
		nvectors = 4;
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
		printk(KERN_INFO "4\n");
		
		regs_start = pci_resource_start(pdev, 0);
		regs_len = pci_resource_len(pdev, 0);
		regs_base_addr = pci_iomap(pdev, 0, 0x100);
		
		/*p = init_interrupt(pdev);
		if(p<0)
		{
			return -1;
		*/
		printk(KERN_INFO "First 4 bytes: %08X\n", ioread32(data_base_addr));
		/* set all masks to on */
		writel(0xffffffff, regs_base_addr + IntrMask);
		//init_interrupt(pdev,4);

		if (init_interrupt(pdev, 4) != 0) {
			printk(KERN_INFO "Could Not Enable MSI-X\n");
		} else {
			printk(KERN_INFO "MSI-X enabled\n");
		}
		//printk(KERN_INFO "5\n");
		iowrite32(0xabcd0023, data_base_addr);
		// msg = ((DEST_ID & 0xff) << 8) + (1 & 0xff);
		// printk("IVSHMEM:  Connecting to %ld\n", DEST_ID);
		// printk("IVSHMEM: Sending Interrupt\n");
		// writel(msg, regs_base_addr + Doorbell);
		
		
		//udelay(500000);
	//	msg = ((DEST_ID & 0xffff) << 16) + (0 & 0xffff);
	//    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	    //memcpy_toio(regs_base_addr + Doorbell, msg, sizeof(uint32_t));
	//    iowrite32(msg, regs_base_addr + Doorbell);
		u32 status;
		// status = ioread32(regs_base_addr + Doorbell);
		// printk(KERN_INFO "Reached Interrupt_handler 0");
		
	    //udelay(500000);
	//	msg = ((DEST_ID & 0xffff) << 16) + (1 & 0xffff);
	//    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	//    iowrite32(msg, regs_base_addr + Doorbell);

	    //udelay(500000);
	//	msg = ((DEST_ID & 0xffff) << 16) + (2 & 0xffff);
	//    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	//    iowrite32(msg, regs_base_addr + Doorbell);

	    //udelay(500000);
	//	msg = ((DEST_ID & 0xffff) << 16) + (3 & 0xffff);
	//    printk(KERN_INFO "IVSHMEM: write 0x%x to Doorbell", msg);
	//    iowrite32(msg, regs_base_addr + Doorbell);

	    // u32 status;

		status = ioread32(regs_base_addr + IVPosition);
		printk(KERN_INFO "IVSHMEM: interrupt (status = 0x%04x)\n",
			   status);





		return 0;

		out_disable:
		  	pci_disable_device(pdev);
	  	return -1;
	}



	static void __exit unload(void)
	{
		int i;
		printk(KERN_INFO "Unloading Module");
		//free_irq(pdev->irq,&pdev);
		printk(KERN_INFO "%d",nvectors);
		for(i=0 ; i<nvectors; ++i)
		{
			free_irq(msix_entries[i].vector, &pdev);
		}
		printk(KERN_INFO "0\n");
		pci_disable_msix(pdev);
		printk(KERN_INFO "1\n");
		pci_iounmap(pdev, regs_base_addr);
		printk(KERN_INFO "2\n");
		pci_iounmap(pdev, data_base_addr);
		printk(KERN_INFO "3\n");
		pci_release_regions(pdev);
		printk(KERN_INFO "4\n");
		pci_disable_device(pdev);
		printk(KERN_INFO "5\n");
		kfree(msix_entries);
		kfree(msix_names);
		
	}



	module_init(initialize);
	module_exit(unload);
	MODULE_LICENSE("GPL");
