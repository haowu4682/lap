#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/wait.h>

#include <asm/uaccess.h>
#include <asm/signal.h>

#include "common.h"

//MODULE_LICENSE("Dual BSD/GPL");

#define LAP_BUF_SIZE (1<<22)

#define LAP_MMIO_ADDR 0x120000000
#define LAP_MMIO_ADDR_SIZE 24

#define LAP_IRQ_NO 13

DECLARE_WAIT_QUEUE_HEAD(lap_queue);

void *lap_mmio_buf = NULL;

int major_num = 0;
int minor_num = 0;
int num_dev = 1;
char *lap_buf = NULL;
int lap_buf_index = 0;
dev_t dev = 0;

struct lap_dev {
    struct cdev cdev; /* Char device structure */
};

struct lap_dev device;


// Declaration of file operations
/*
* Open and close
*/

int lap_open(struct inode *inode, struct file *filp)
{
#if 0
    struct lap_dev *dev; /* device information */

    dev = container_of(inode->i_cdev, struct lap_dev, cdev);
    filp->private_data = dev; /* for other methods */

    /* now trim to 0 the length of the device if open was write-only */
    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        lap_trim(dev); /* ignore errors */
        up(&dev->sem);
    }
#endif

    lap_buf_index = 0;

    return 0;          /* success */

}

int lap_release(struct inode *inode, struct file *filp)
{
    lap_buf_index = 0;

    return 0;
}

ssize_t lap_write(struct file *filp, const char __user *buf, size_t count, 
        loff_t *f_pos)
{
    long failed_bytes_count;

    if (lap_buf_index + count >= LAP_BUF_SIZE) {
        // Buffer size exceeded, cannot copy the data
        return -ENOSPC;
    }

    failed_bytes_count = copy_from_user(lap_buf + lap_buf_index, buf, count);
    lap_buf_index += count - failed_bytes_count;

    return count - failed_bytes_count;
}

ssize_t lap_read(struct file *filp, char __user *buf, size_t count, 
        loff_t *f_pos)
{
    int i;
    long failed_bytes_count;

    //phys_addr_t lap_phys_addr = virt_to_phys(lap_buf);
    uint64_t lap_virt_addr = (uint64_t) lap_buf;

#if 0
    // Execute the LAP code
    asm("push %%rdi;\
         mov %1, %%rdi; \
         xlat; \
         pop %%rdi;"
         : "=m"(lap_buf)
         : "m"(lap_buf));
#endif

    //*((uint64_t *)LAP_MMIO_ADDR) = 1;

    iowrite32(lap_virt_addr & 0xffffffff, lap_mmio_buf+8);
    iowrite32(lap_virt_addr >> 32, lap_mmio_buf+12);
    iowrite32(0xffffffff, lap_mmio_buf);

    // Polling for LAP_MMIO_ADDR
    //while (*((uint64_t *)LAP_MMIO_ADDR) != 0)
    while (ioread32(lap_mmio_buf) != 0) {
        wait_event_interruptible(lap_queue, ioread32(lap_mmio_buf) == 0);
    }

    // TODO Use a more elegant way to allow user to define reading pattern
    // Read from beginning
    lap_buf_index = 0;

    // Return the result
    if (lap_buf_index + count >= LAP_BUF_SIZE) {
        count = LAP_BUF_SIZE - lap_buf_index;
    }

    for (i = 0; i < count; i = i + 4) {
        printk("%d ", *((int *)(lap_buf + i)));
    }
    printk("\n");

    failed_bytes_count = copy_to_user(buf, lap_buf + lap_buf_index, count);
    //count - failed_bytes_count;

    return count - failed_bytes_count;
}

// MMAP function
int lap_mmap(struct file *filp, struct vm_area_struct *vma)
{
#if 0
    if (remap_pfn_range(vma, vma->vm_start, vm->vm_pgoff,
                vma->vm_end - vma->vm_start,
                vma->vm_page_prot))
        return -EAGAIN;
    //    vma->vm_ops = &simple_remap_vm_ops;
    //    simple_vma_open(vma);
#endif

    return 0;
}

irqreturn_t lap_handle_irq(int irq, void *dev_id)
{
    printk (KERN_DEBUG "Inside the lap handler!\n");
    wake_up_interruptible(&lap_queue);
    return IRQ_HANDLED;
}

struct file_operations lap_fops = {
    .owner      =   THIS_MODULE,
    //    .mmap       =   lap_mmap,
    //    .llseek =   lap_llseek,
    .read =     lap_read,
    .write =    lap_write,
    //    .ioctl =    lap_ioctl,
    .open =     lap_open,
    .release =  lap_release,
};

static void setup_dev(struct lap_dev *dev)
{
    //assert(dev != NULL);

    int err, devno = MKDEV(major_num, minor_num);

    cdev_init(&dev->cdev, &lap_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &lap_fops;
    err = cdev_add (&dev->cdev, devno, 1);

    /* Fail gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding lap device", err);
}

static int lap_init(void)
{
    int result;
    struct resource *res;
    unsigned int lap_mmio_data;

    //printk(KERN_ALERT "Hello, world\n");
    // Initialiate LAP buffer
    lap_buf = kmalloc(LAP_BUF_SIZE, GFP_KERNEL);

    // Initialiate LAP MMIO memory
    res = request_mem_region(LAP_MMIO_ADDR, LAP_MMIO_ADDR_SIZE, "LAP mmio address");
    if (res == NULL) {
        printk(KERN_ALERT "lap: cannot request the MMIO used by LAP, aborted.\n");
        return -1;
    }

    lap_mmio_buf = ioremap_nocache(LAP_MMIO_ADDR, LAP_MMIO_ADDR_SIZE);
    iowrite32(0, lap_mmio_buf);
    lap_mmio_data = ioread32(lap_mmio_buf);
    printk(KERN_DEBUG "lap: lap_mmio_buf = %p, lap_mmio_data = %u\n", lap_mmio_buf, lap_mmio_data);
    printk(KERN_DEBUG "lap: lap_buf = %p\n", lap_buf);

    // Initiazlie driver number
    if (major_num) {
        dev = MKDEV(major_num, minor_num);
        result = register_chrdev_region(dev, num_dev, "lap");
    } else {
        result = alloc_chrdev_region(&dev, minor_num, num_dev,
                "lap");
        major_num = MAJOR(dev);
    }

    if (result < 0) {
        printk(KERN_WARNING "lap: can't get major %d\n", major_num);
        return result;
    }

    // Initialize device
    setup_dev(&device);

    // Register Interrupt Handler
    result = request_irq(LAP_IRQ_NO, lap_handle_irq, 0 /* flags */, "lap", NULL);

    //major_num = register_blkdev(0, "lap");
    return 0;
}

static void lap_exit(void)
{
    unregister_chrdev_region(dev, minor_num);
    release_mem_region(LAP_MMIO_ADDR, LAP_MMIO_ADDR_SIZE);

    //printk(KERN_ALERT "Goodbye, cruel world\n");
    kfree(lap_buf);
    lap_buf = NULL;
}

module_init(lap_init);
module_exit(lap_exit);

