/**
 * File: asgn2.c
 * Date: 13/03/2011
 * Modified: 19/09/2015
 * Author: Ashley Manson 
 * Version: 1.0
 *
 * This is a module which serves as a virtual ramdisk which disk size is
 * limited by the amount of memory available and serves as the requirement for
 * COSC440 assignment 2 in 2015.
 *
 * Note: multiple devices and concurrent modules are not supported in this
 *       version.
 */

/**
 * This program is free software; you can redistribute it and/or
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
#include "gpio.h"

#define MYDEV_NAME "asgn2"
#define MYIOC_TYPE 'k'

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ashley Manson");
MODULE_DESCRIPTION("COSC440 asgn2");

/**
 * The node structure for the memory page linked list.
 */ 
typedef struct page_node_rec {
    struct list_head list;
    struct page *page;
} page_node;

typedef struct asgn2_dev_t {
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
} asgn2_dev;

asgn2_dev asgn2_device;

int asgn2_major = 0;     /* major number of module */  
int asgn2_minor = 0;     /* minor number of module */
int asgn2_dev_count = 1; /* number of devices */

static struct proc_dir_entry *asgn2_proc;

/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {

    int page_num = 0;
    page_node *curr, *tmp;

    printk(KERN_INFO "asgn2: free_memory_pages called\n");

    // loop through the list, while freeing all the pages
    list_for_each_entry_safe(curr, tmp, &asgn2_device.mem_list, list) {
        if (curr != NULL) {
            printk(KERN_INFO "asgn2: Freeing memory page %d\n", page_num++);
            __free_page(curr->page);
            list_del(&curr->list);
            kfree(curr);
        }
    }

    // reset data_size and num_pages
    asgn2_device.data_size = 0;
    asgn2_device.num_pages = 0;
    
    printk(KERN_INFO "asgn2: free_memory_pages finished\n");
}

/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn2_open(struct inode *inode, struct file *filp) {

    int num_procs = atomic_read(&asgn2_device.nprocs);
    int max_num_procs = atomic_read(&asgn2_device.max_nprocs);

    printk(KERN_INFO "asgn2: asgn2_open called\n");
    
    if (num_procs >= max_num_procs) {
        printk(KERN_WARNING "asgn2: Device already in use!\n");
        return -EBUSY;
    }
    
    atomic_inc(&asgn2_device.nprocs);
    printk(KERN_INFO "asgn2: Process count incremented to %d\n", atomic_read(&asgn2_device.nprocs));

    // If opened in write-only
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        printk(KERN_INFO "asgn2: Opened in write-only\n");
        free_memory_pages();
    }
    
    printk(KERN_INFO "asgn2: asgn2_open finished\n");

    return 0;
}

/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn2_release (struct inode *inode, struct file *filp) {

    printk(KERN_INFO "asgn2: asgn2_release called\n");
    
    atomic_dec(&asgn2_device.nprocs);
    printk(KERN_INFO "asgn2: Process count decremented to %d\n", atomic_read(&asgn2_device.nprocs));

    printk(KERN_INFO "asgn2: asgn2_release finished\n");

    return 0;
}

/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {

    size_t size_read = 0;                     /* size read from virtual disk in this function */
    size_t begin_offset = *f_pos % PAGE_SIZE; /* the offset from the beginning of a page to start reading */
    int begin_page_no = *f_pos / PAGE_SIZE;   /* the first page which contains the requested data */
    int curr_page_no = -1;                    /* the current page number */
    size_t curr_size_read;                    /* size read from the virtual disk in this round */
    size_t size_to_be_read;                   /* size to be read in the current round in while loop */
    size_t size_to_read;                      /* size left to read from kernel space */
    size_t size_from_pages;                   /* maximum size to read from all pages */
    page_node *curr;                          /* the current node in the list */

    printk(KERN_INFO "asgn2: asgn2_read called\n");
    printk(KERN_INFO "asgn2: Number of pages %d\n", asgn2_device.num_pages);

    if (*f_pos > asgn2_device.data_size) {
        printk(KERN_WARNING "asgn2: f_pos (%d) > data_size (%d)\n", (int)*f_pos, (int)asgn2_device.data_size);
        return 0;
    }

    size_from_pages = min(count, asgn2_device.data_size - (size_t)*f_pos);

    // loop through the list, reading the contents of each page
    list_for_each_entry(curr, &asgn2_device.mem_list, list) {
        // if we have reached the starting page to read from
        if (++curr_page_no == begin_page_no) {
            size_to_read = min((int)(PAGE_SIZE - begin_offset), (int)(size_from_pages - size_read));
            printk(KERN_INFO "asgn2: Reading from page %d with size %d\n", curr_page_no, size_to_read);
            size_to_be_read = copy_to_user(buf + size_read, page_address(curr->page) + begin_offset, size_to_read);
            printk(KERN_INFO "asgn2: Size left to read = %d\n", size_to_be_read);
            curr_size_read = size_to_read - size_to_be_read;
            size_to_read = size_to_be_read;
            size_read += curr_size_read;
            printk(KERN_INFO "asgn2: size_from_pages %d, size_read %d\n", size_from_pages, size_read);
            // if still more to read
            if (size_from_pages != size_read) {
                begin_page_no++;  // go to next page
                begin_offset = 0; // offset at start of page
                printk(KERN_INFO "asgn2: Read from the next page %d\n", begin_page_no);
            }
            // nothing else to read
            else {
                printk(KERN_INFO "asgn2: Nothing left to read\n");
                break;
            }
        }
    }

    *f_pos += size_read;
    
    printk(KERN_INFO "asgn2: size_read = %d\n", size_read);
    
    printk(KERN_INFO "asgn2: asgn2_read finished\n");
    
    return size_read;
}

/**
 * This function writes from the user buffer to the virtual disk of this
 * module
 */
/**
ssize_t asgn2_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {

    size_t orig_f_pos = *f_pos;               / * the original file position * /
    size_t size_written = 0;                  / * size written to virtual disk in this function * /
    size_t begin_offset = *f_pos % PAGE_SIZE; / * the offset from the beginning of a page to start writing * /
    int begin_page_no = *f_pos / PAGE_SIZE;   / * the first page this function should start writing to * /
    int curr_page_no = -1;                    / * the current page number * /
    size_t curr_size_written;                 / * size written to virtual disk in this round * /
    size_t size_to_be_written;                / * size to be read in the current round in while loop * /
    size_t size_to_write = count;             / * size left to copy over from user space * /
    page_node *curr;                          / * the current node in the list * /

    printk(KERN_INFO "asgn2: asgn2_write called\n");
    printk(KERN_INFO "asgn2: *f_pos + count = %d\n", (int)(*f_pos + count));
      
    // add pages if necessary
    while (asgn2_device.num_pages * PAGE_SIZE < *f_pos + count) {
        curr = kmalloc(sizeof(page_node), GFP_KERNEL);
        if (curr != NULL) {
            curr->page = alloc_page(GFP_KERNEL);
            if (curr->page == NULL) {
                printk(KERN_WARNING "asgn2: Page allocation failed!\n");
                return size_written;
            }
            list_add_tail(&curr->list, &asgn2_device.mem_list);
            asgn2_device.num_pages++;
            printk(KERN_INFO "asgn2: Added pages to list: %d\n", asgn2_device.num_pages);
        }
        else {
            printk(KERN_WARNING "asgn2: Couldn't add pages to list!\n");
            return size_written;
        }
    }

    // loop through the list, writing to each page
    list_for_each_entry(curr, &asgn2_device.mem_list, list) {
        // if we have reached the starting page to write
        if (++curr_page_no == begin_page_no) {
            size_to_write = min((int)(PAGE_SIZE - begin_offset), (int)(count - size_written));
            printk(KERN_INFO "asgn2: Writing to page %d with size %d\n", curr_page_no, size_to_write);
            size_to_be_written = copy_from_user(page_address(curr->page) + begin_offset, buf + size_written, size_to_write);
            printk(KERN_INFO "asgn2: Size left to write = %d\n", size_to_be_written);
            curr_size_written = size_to_write - size_to_be_written;
            size_to_write = size_to_be_written;
            size_written += curr_size_written;
            printk(KERN_INFO "asgn2: count %d, size_written %d\n", count, size_written);
            // if still more to write
            if (size_written != count) {
                begin_page_no++;  // go to next page
                begin_offset = 0; // offset at start of page
                printk(KERN_INFO "asgn2: Write to the next page %d\n", begin_page_no);
            }
            // nothing else to write
            else {
                printk(KERN_INFO "asgn2: Nothing left to write\n");
                break;
            }
        }
    }

    *f_pos += size_written;
    
    printk(KERN_INFO "asgn2: size_written = %d\n", size_written);
    
    asgn2_device.data_size = max(asgn2_device.data_size, orig_f_pos + size_written);
    
    printk(KERN_INFO "asgn2: asgn2_write finished\n");
    
    return size_written;
}
*/

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn2_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {

    int nr = _IOC_NR(cmd);
    int new_nprocs;
    int result;

    printk(KERN_INFO "asgn2: asgn2_ioctl called\n");

    // check if cmd is for this device
    if (_IOC_TYPE(cmd) != MYIOC_TYPE) {
        printk(KERN_WARNING "asgn2: Got invalid case cmd = %d\n", cmd);
        return -EINVAL;
    }
    
    switch(nr) {
    case SET_NPROC_OP:
        result = access_ok(VERIFY_READ, arg, sizeof(int));
        if (!result) {
            printk(KERN_WARNING "asgn2: ioctl argument is not valid!\n");
            return -EFAULT;
        }
        result = get_user(new_nprocs, (int *)arg);
        if (new_nprocs < 0) {
            printk(KERN_WARNING "asgn2: user tried to set max_nprocs to less than 0!\n");
            return -EINVAL;
        }
        atomic_set(&asgn2_device.max_nprocs, new_nprocs);
        break;
    default:
        return -ENOTTY;
    }

    printk(KERN_INFO "asgn2: asgn2_ioctl finished\n");
    
    return 0;
}

/**
 * Displays information about current status of the module,
 * which helps debugging.
 */
int asgn2_read_procmem(char *buf, char **start, off_t offset, int count, int *eof, void *data) {

    *eof = 1;
    return snprintf(buf, count, "nprocs %d, max_nprocs %d\nnum_pages %d, data_size %d\n",
                      atomic_read(&asgn2_device.nprocs),
                      atomic_read(&asgn2_device.max_nprocs),
                      asgn2_device.num_pages,
                      asgn2_device.data_size);
}

struct file_operations asgn2_fops = {
    .owner = THIS_MODULE,
    .read = asgn2_read,
    //.write = asgn2_write,
    .unlocked_ioctl = asgn2_ioctl,
    .open = asgn2_open,
    .release = asgn2_release,
};

/**
 * Initialise the module and create the master device
 */
int __init asgn2_init_module(void) {

    int result; 

    atomic_set(&asgn2_device.nprocs, 0);
    atomic_set(&asgn2_device.max_nprocs, 1);
    result = alloc_chrdev_region(&asgn2_device.dev, asgn2_minor, asgn2_dev_count, MYDEV_NAME);
    if (result < 0)
        goto fail_device;
    asgn2_device.cdev = cdev_alloc();
    asgn2_device.cdev->ops = &asgn2_fops;
    asgn2_device.cdev->owner = asgn2_fops.owner;
    result = cdev_add(asgn2_device.cdev, asgn2_device.dev, asgn2_dev_count);
    if (result < 0)
        goto fail_device;
    INIT_LIST_HEAD(&asgn2_device.mem_list);
    asgn2_proc = create_proc_entry(MYDEV_NAME, 777, NULL);
    if (!asgn2_proc) {
        printk(KERN_INFO "asgn2: Failed to create proc entry %s\n", MYDEV_NAME);
        return -ENOMEM;
    }
    asgn2_proc->read_proc = asgn2_read_procmem;

    gpio_dummy_init();
    
    asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
    if (IS_ERR(asgn2_device.class)) {
    }

    asgn2_device.device = device_create(asgn2_device.class, NULL, 
                                        asgn2_device.dev, "%s", MYDEV_NAME);
    if (IS_ERR(asgn2_device.device)) {
        printk(KERN_WARNING "asgn2: %s: can't create udev device\n", MYDEV_NAME);
        result = -ENOMEM;
        goto fail_device;
    }

    printk(KERN_WARNING "asgn2: set up udev entry\n");
    printk(KERN_WARNING "asgn2: Hello world from %s\n", MYDEV_NAME);
    return 0;

fail_device:
    class_destroy(asgn2_device.class);

    free_memory_pages();
    if (asgn2_device.dev)
        unregister_chrdev_region(asgn2_device.dev, 1);
    if (asgn2_device.class)
        class_destroy(asgn2_device.class);
    if (asgn2_proc)
        remove_proc_entry(MYDEV_NAME, NULL);
    if (asgn2_device.cdev)
        cdev_del(asgn2_device.cdev);
    
    return result;
}

/**
 * Finalise the module
 */
void __exit asgn2_exit_module(void) {

    gpio_dummy_exit();
    
    device_destroy(asgn2_device.class, asgn2_device.dev);
    class_destroy(asgn2_device.class);
    
    printk(KERN_WARNING "asgn2: cleaned up udev entry\n");

    free_memory_pages();
    unregister_chrdev_region(asgn2_device.dev, 1);
    remove_proc_entry(MYDEV_NAME, NULL);
    cdev_del(asgn2_device.cdev);

    printk(KERN_WARNING "asgn2: Good bye from %s\n", MYDEV_NAME);
}

module_init(asgn2_init_module);
module_exit(asgn2_exit_module);
