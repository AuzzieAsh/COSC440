/**
 * File: asgn1.c
 * Date: 13/03/2011
 * Modified: 01/09/2015
 * Author: Ashley Manson 
 * Version: 0.1
 *
 * This is a module which serves as a virtual ramdisk which disk size is
 * limited by the amount of memory available and serves as the requirement for
 * COSC440 assignment 1 in 2015.
 *
 * Note: multiple devices and concurrent modules are not supported in this
 *       version.
 */

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/device.h>

#define MYDEV_NAME "asgn1"
#define MYIOC_TYPE 'k'

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ashley Manson");
MODULE_DESCRIPTION("COSC440 asgn1");

/**
 * The node structure for the memory page linked list.
 */ 
typedef struct page_node_rec {
    struct list_head list;
    struct page *page;
} page_node;

typedef struct asgn1_dev_t {
    dev_t dev;                /* the device */
    struct cdev *cdev;
    struct list_head mem_list; 
    int num_pages;            /* number of memory pages this module currently holds */
    size_t data_size;         /* total data size in this module */
    atomic_t nprocs;          /* number of processes accessing this device */ 
    atomic_t max_nprocs;      /* max number of processes accessing this device */
    struct kmem_cache *cache; /* cache memory */
    struct class *class;      /* the udev class */
    struct device *device;    /* the udev device node */
} asgn1_dev;

asgn1_dev asgn1_device;

int asgn1_major = 0;     /* major number of module */  
int asgn1_minor = 0;     /* minor number of module */
int asgn1_dev_count = 1; /* number of devices */

static struct proc_dir_entry *asgn1_proc;

/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {
    /* Finished. */
  
    page_node *curr, *tmp;

    printk(KERN_INFO "asgn1: free_memory_pages called\n");
    
    list_for_each_entry_safe(curr, tmp, &asgn1_device.mem_list, list) {
        if (curr != NULL) {
            if (curr->page != NULL) {
                __free_page(curr->page);
            }
            list_del(&curr->list);
            kfree(curr);
        }
    }
    asgn1_device.data_size = 0;
    asgn1_device.num_pages = 0;
    
    printk(KERN_INFO "asgn1: free_memory_pages finished\n");
}

/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn1_open(struct inode *inode, struct file *filp) {
    /* Finished. */

    int num_procs = atomic_read(&asgn1_device.nprocs);
    int max_num_procs = atomic_read(&asgn1_device.max_nprocs);

    printk(KERN_INFO "asgn1: asgn1_open called\n");
    printk(KERN_INFO "asgn1: nprocs = %d, max_nprocs = %d\n", num_procs, max_num_procs);
    
    if (num_procs > max_num_procs)
        return -EBUSY;
    
    atomic_inc(&asgn1_device.nprocs);
    printk(KERN_INFO "asgn1: Process count incremented to %d\n", atomic_read(&asgn1_device.nprocs));

    // If opened in write-only
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        free_memory_pages();
    
    printk(KERN_INFO "asgn1: asgn1_open finished\n");

    return 0;
}

/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn1_release (struct inode *inode, struct file *filp) {
    /* Finished. */

    printk(KERN_INFO "asgn1: asgn1_release called\n");
    
    atomic_dec(&asgn1_device.nprocs);
    printk(KERN_INFO "asgn1: Process count decremented to %d\n", atomic_read(&asgn1_device.nprocs));

    printk(KERN_INFO "asgn1: asgn1_release finished\n");

    return 0;
}

/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn1_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {

    size_t size_read = 0;                     /* size read from virtual disk in this function */
    size_t begin_offset = *f_pos % PAGE_SIZE; /* the offset from the beginning of a page to start reading */
    int begin_page_no = *f_pos / PAGE_SIZE;   /* the first page which contains the requested data */
    int curr_page_no = 0;                     /* the current page number */
    size_t curr_size_read;                    /* size read from the virtual disk in this round */
    size_t size_to_be_read;                   /* size to be read in the current round in while loop */
    size_t size_to_read = count;              /* size left to read from kernel space */
   
    struct list_head *ptr = asgn1_device.mem_list.next;
    page_node *curr;

    /* Finished? */

    printk(KERN_INFO "asgn1: asgn1_read called\n");
    
    if (*f_pos > asgn1_device.data_size) {
        printk(KERN_INFO "asgn1: f_pos (%d) is greater then data_size (%d)\n", (int)*f_pos, (int)asgn1_device.data_size);
        return 0;
    }

    if (count > asgn1_device.data_size) {
        printk(KERN_INFO "asgn1: Attempted to read more then data_size!\n");
        size_to_read = asgn1_device.data_size;
    }
    
    list_for_each_entry(curr, ptr, list) {
        // if we have reached the starting page to read from
        if (curr_page_no++ == begin_page_no) {
            printk(KERN_INFO "asgn1: Reading from page %d with size %d\n", curr_page_no, size_to_read);
            size_to_be_read = copy_to_user(buf, page_address(curr->page) + begin_offset, size_to_read);
            printk(KERN_INFO "asgn1: Size left to read = %d\n", size_to_be_read);
            curr_size_read = size_to_read - size_to_be_read;
            size_to_read = size_to_be_read;
            size_read += curr_size_read;
            // if still more to read
            if (size_to_be_read > 0) {
                begin_page_no++;  // go to next page
                begin_offset = 0; // offset at start of page
                printk(KERN_INFO "asgn1: Read from the next page %d\n", begin_page_no);
            }
            // nothing else to read
            else {
                printk(KERN_INFO "asgn1: Nothing left to read\n");
                break;
            }
        }
    }

    printk(KERN_INFO "asgn1: size_read = %d\n", size_read);
    
    printk(KERN_INFO "asgn1: asgn1_read finished\n");
    
    return size_read;
}

static loff_t asgn1_lseek (struct file *file, loff_t offset, int cmd) {
    /* Finished. */
    
    loff_t testpos;
    size_t buffer_size = asgn1_device.num_pages * PAGE_SIZE;

    switch(cmd) {
    case SEEK_SET:
        testpos = offset;
        break;
    case SEEK_CUR:
        testpos = file->f_pos + offset;
        break;
    case SEEK_END:
        testpos = buffer_size + offset;
        break;
    default:
        return -EINVAL;
    }
    
    if (testpos > buffer_size)
        testpos = buffer_size;
    else if (testpos < 0)
        testpos = 0;

    file->f_pos = testpos;
    
    printk (KERN_INFO "asgn1: Seeking to pos=%ld\n", (long)testpos);

    return testpos;
}

/**
 * This function writes from the user buffer to the virtual disk of this
 * module
 */
ssize_t asgn1_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {

    size_t orig_f_pos = *f_pos;               /* the original file position */
    size_t size_written = 0;                  /* size written to virtual disk in this function */
    size_t begin_offset = *f_pos % PAGE_SIZE; /* the offset from the beginning of a page to start writing */
    int begin_page_no = *f_pos / PAGE_SIZE;   /* the first page this function should start writing to */
    int curr_page_no = 0;                     /* the current page number */
    size_t curr_size_written;                 /* size written to virtual disk in this round */
    size_t size_to_be_written;                /* size to be read in the current round in while loop */
    size_t size_to_copy = count;              /* size left to copy over from user space */
    
    struct list_head *ptr = asgn1_device.mem_list.next;
    page_node *curr;

    /* Finished? */

    printk(KERN_INFO "asgn1: asgn1_write called\n");
    
    // add pages if necessary
    while (asgn1_device.num_pages * PAGE_SIZE < *f_pos + count) {
        curr = kmalloc(sizeof(page_node), GFP_KERNEL);
        if (curr != NULL) {
            curr->page = alloc_page(GFP_KERNEL);
            list_add_tail(&curr->list, ptr);
            asgn1_device.num_pages++;
            printk(KERN_INFO "asgn1: Added pages to list: %d\n", asgn1_device.num_pages);
        }
        else {
            printk(KERN_INFO "asgn1: Couldn't add pages to list!\n");
            return size_written;
        }
    }

    list_for_each_entry(curr, ptr, list) {
        // if we have reached the starting page to write
        if (curr_page_no++ == begin_page_no) {
            printk(KERN_INFO "asgn1: Writing to page %d with size %d\n", curr_page_no, size_to_copy);
            size_to_be_written = copy_from_user(page_address(curr->page) + begin_offset, buf, size_to_copy);
            printk(KERN_INFO "asgn1: Size left to write = %d\n", size_to_be_written);
            curr_size_written = size_to_copy - size_to_be_written;
            size_to_copy = size_to_be_written;
            size_written += curr_size_written;
            // if still more to write
            if (size_to_be_written > 0) {
                begin_page_no++;  // go to next page
                begin_offset = 0; // offset at start of page
                printk(KERN_INFO "asgn1: Write to the next page %d\n", begin_page_no);
                // may need to add another page here
            }
            // nothing else to write
            else {
                printk(KERN_INFO "asgn1: Nothing left to write\n");
                break;
            }
        }
    }

    printk(KERN_INFO "asgn1: size_written = %d\n", size_written);
    
    asgn1_device.data_size = max(asgn1_device.data_size, orig_f_pos + size_written);
    
    printk(KERN_INFO "asgn1: asgn1_write finished\n");
    
    return size_written;
}

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn1_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {

    int nr;
    int new_nprocs;
    int result;

    /* COMPLETE ME */
    /** 
     * check whether cmd is for our device, if not for us, return -EINVAL 
     *
     * get command, and if command is SET_NPROC_OP, then get the data, and
     * set max_nprocs accordingly, don't forget to check validity of the 
     * value before setting max_nprocs
     */
    if (_IOC_TYPE(cmd) != TEM_SET_NPROC)
        return -EINVAL;
    /*
    switch(cmd) {
    case SET_NPROC_OP:
        result = access_ok(VERIFY_READ, filp->f_pos, arg);
        if (result)
            get_user(&new_nprocs, filp->f_pos);
        else
            return -EINVAL;
        atomic_set(&asgn1_device.max_nprocs, new_nprocs);
        return 0;
    default:
        return -ENOTTY;
    }*/
    return 0;
}

/**
 * Displays information about current status of the module,
 * which helps debugging.
 */
int asgn1_read_procmem(char *buf, char **start, off_t offset, int count, int *eof, void *data) {

    /* stub */
    int result;

    /* COMPLETE ME */
    /**
     * use snprintf to print some info to buf, up to size count
     * set eof
     */
    *eof = 1;
    result = snprintf(buf, count, "%s\n", "use snprintf to print some info to buf, up to size count");
    return result;
}

static int asgn1_mmap (struct file *filp, struct vm_area_struct *vma) {

    unsigned long pfn;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long len = vma->vm_end - vma->vm_start;
    unsigned long ramdisk_size = asgn1_device.num_pages * PAGE_SIZE;
    page_node *curr;
    unsigned long index = 0;

    /* COMPLETE ME */
    /**
     * check offset and len
     *
     * loop through the entire page list, once the first requested page
     *   reached, add each page with remap_pfn_range one by one
     *   up to the last requested page
     */
    if (offset + len > ramdisk_size)
        return -EINVAL;
    /*  
    list_for_each(curr, list) {
        if (index >= offset)
            break;
        index++;
    }
    */
    if (remap_pfn_range(vma, vma->vm_start, offset, len, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}

struct file_operations asgn1_fops = {
    .owner = THIS_MODULE,
    .read = asgn1_read,
    .write = asgn1_write,
    .unlocked_ioctl = asgn1_ioctl,
    .open = asgn1_open,
    .mmap = asgn1_mmap,
    .release = asgn1_release,
    .llseek = asgn1_lseek
};

/**
 * Initialise the module and create the master device
 */
int __init asgn1_init_module(void) {

    int result; 

    /* Finished? */
    /**
     * set nprocs and max_nprocs of the device
     *
     * allocate major number
     * allocate cdev, and set ops and owner field 
     * add cdev
     * initialize the page list
     * create proc entries
     */
    atomic_set(&asgn1_device.nprocs, 0);
    atomic_set(&asgn1_device.max_nprocs, 1);
    result = alloc_chrdev_region(&asgn1_device.dev, asgn1_minor, asgn1_dev_count, MYDEV_NAME);
    asgn1_device.cdev = cdev_alloc();
    asgn1_device.cdev->ops = &asgn1_fops;
    asgn1_device.cdev->owner = asgn1_fops.owner;
    result = cdev_add(asgn1_device.cdev, asgn1_device.dev, asgn1_dev_count);
    INIT_LIST_HEAD(&asgn1_device.mem_list);
    asgn1_proc = create_proc_entry(MYDEV_NAME, 666, NULL);
    if (!asgn1_proc) {
        printk(KERN_INFO "asgn1: Failed to create proc entry %s\n", MYDEV_NAME);
        return -ENOMEM;
    }
    asgn1_proc->read_proc = asgn1_read_procmem;

    asgn1_device.class = class_create(THIS_MODULE, MYDEV_NAME);
    if (IS_ERR(asgn1_device.class)) {
    }

    asgn1_device.device = device_create(asgn1_device.class, NULL, 
                                        asgn1_device.dev, "%s", MYDEV_NAME);
    if (IS_ERR(asgn1_device.device)) {
        printk(KERN_WARNING "asgn1: %s: can't create udev device\n", MYDEV_NAME);
        result = -ENOMEM;
        goto fail_device;
    }

    printk(KERN_WARNING "asgn1: set up udev entry\n");
    printk(KERN_WARNING "asgn1: Hello world from %s\n", MYDEV_NAME);
    return 0;

    /* cleanup code called when any of the initialization steps fail */
fail_device:
    class_destroy(asgn1_device.class);

    /* Finished? */
    /* PLEASE PUT YOUR CLEANUP CODE HERE, IN REVERSE ORDER OF ALLOCATION */
    free_memory_pages();

    if (asgn1_proc)
        remove_proc_entry(MYDEV_NAME, NULL);

    return result;
}

/**
 * Finalise the module
 */
void __exit asgn1_exit_module(void) {

    device_destroy(asgn1_device.class, asgn1_device.dev);
    class_destroy(asgn1_device.class);
    printk(KERN_WARNING "asgn1: cleaned up udev entry\n");

    /* Finished? */
    /**
     * free all pages in the page list 
     * cleanup in reverse order
     */
    free_memory_pages();
    if (asgn1_proc)
        remove_proc_entry(MYDEV_NAME, NULL);

    printk(KERN_WARNING "asgn1: Good bye from %s\n", MYDEV_NAME);
}

module_init(asgn1_init_module);
module_exit(asgn1_exit_module);
