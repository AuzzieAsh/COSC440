/**
 * File: asgn.c
 * Date: 26/09/2015
 * Author: Ashley Manson 
 * Version: 1.1
 *
 * This is a module which serves as a driver for a dummpy GPIO port device
 * and serves as the requirement for COSC440 assignment 2 in 2015.
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
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/circ_buf.h>
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

/**
 * The device structure
 */
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

/**
 * circular buffer for temp storage
 */
#define BUFFER_CAPACITY 64
typedef struct circular_buf_t {
    int head;
    int count;
    u8 buffer[BUFFER_CAPACITY];
} circular_buf;

/**
 * keep track of sessions/files
 */
#define MAX_SESSIONS 100
typedef struct sessions_tracker_t {
    int head;
    int count;
    size_t read_offset;
    size_t write_offset;
    size_t sizes[MAX_SESSIONS];
} sessions_tracker;

/**
 * Global Variables
 */
asgn2_dev asgn2_device;                   /* device for module */
int asgn2_major = 0;                      /* major number of module */  
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */
static struct proc_dir_entry *asgn2_proc; /* proc entry for device */
static DECLARE_WAIT_QUEUE_HEAD(wq);       /* the wait queue */
int toggle = 1;                           /* toggle for first/second part of half byte */
u8 top_full_byte = 0;                     /* full byte read in from top half */
circular_buf cbuf;                        /* circular buffer temp storage */
sessions_tracker sessions;                /* struct to keep track of sessions */
int fin_reading = 0;

/**
 * Bottom Half
 * Copy from buffer to list
 */
void tasklet_copy(unsigned long data) {

    u8 byte;
    size_t begin_offset;
    struct list_head *ptr;
    page_node *curr;
    
    // while something in cbuf, copy to page list
    while (cbuf.count != 0) {
        byte = cbuf.buffer[cbuf.head];
        cbuf.head = (cbuf.head + 1) % BUFFER_CAPACITY;
        cbuf.count--;
        begin_offset = sessions.write_offset;
        ptr = asgn2_device.mem_list.next;
        curr = list_entry(ptr, page_node, list);

        // if we are starting on a new page, allocate it
        if (begin_offset == 0) {
            curr = kmalloc(sizeof(page_node), GFP_KERNEL);
            if (curr != NULL) {
                curr->page = alloc_page(GFP_KERNEL);
                if (curr->page == NULL) {
                    printk(KERN_WARNING "asgn2: Page allocation failed!\n");
                    return;
                }
                list_add_tail(&curr->list, &asgn2_device.mem_list);
                asgn2_device.num_pages++;
                printk(KERN_INFO "asgn2: Added a page to list: %d\n", asgn2_device.num_pages);
            }
            else {
                printk(KERN_WARNING "asgn2: Couldn't add pages to list!\n");
                return;
            }
        }
        // Copy byte to page
        memcpy(page_address(curr->page) + begin_offset, &byte, sizeof(byte));
        asgn2_device.data_size += sizeof(byte);
        sessions.write_offset = (sessions.write_offset + sizeof(byte)) % PAGE_SIZE;
        printk(KERN_INFO "asgn2: write_offset = %d\n", sessions.write_offset);
    }
}

DECLARE_TASKLET(buffer_copy, tasklet_copy, 0);

/**
 * Top Half
 * Add bytes to buffer
 */
irqreturn_t dummyport_interrupt(int irq, void *dev_id) {

    u8 read_byte = read_half_byte();
    int cbuf_end = (cbuf.head + cbuf.count) % BUFFER_CAPACITY;
    int session_end = (sessions.head + sessions.count) % MAX_SESSIONS;
    
    // First part of byte
    if (toggle) {
        toggle = 0;
        top_full_byte = read_byte << 4;
    }
    // Second part of byte
    else {
        toggle = 1;
        top_full_byte = top_full_byte | read_byte;
        
        printk(KERN_INFO "asgn2: Assembled full byte: %d\n", top_full_byte);

        if (top_full_byte == '\0') {
            sessions.count++;
            printk(KERN_INFO "asgn2: Session/File size: %d\n", sessions.sizes[session_end]);
        }
        else {
            if (cbuf.count >= BUFFER_CAPACITY) {
                printk(KERN_WARNING "asgn2: Temporary buffer is full, dropping most recent byte\n");
                return IRQ_HANDLED;
            }
            
            // store in circular buffer
            cbuf.buffer[cbuf_end] = top_full_byte;
            cbuf.count++;
            sessions.sizes[session_end] += sizeof(top_full_byte);
        }
        
        // schedule tasklet to copy from circular buffer
        tasklet_schedule(&buffer_copy);
        printk(KERN_INFO "asgn2: Tasklet scheduled\n");
    }
    return IRQ_HANDLED;
}

/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {

    int page_num = 0;
    page_node *curr, *tmp;

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
    
    printk(KERN_INFO "asgn2: Freed all pages\n");
}

/**
 * This function opens the virtual disk.
 */
int asgn2_open(struct inode *inode, struct file *filp) {

    // If opened in write-only
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR) {
        printk(KERN_INFO "asgn2: Cannot be open to write too!\n");
        return -EACCES;
    }
    
    if (atomic_read(&asgn2_device.nprocs) >= atomic_read(&asgn2_device.max_nprocs)) {
        printk(KERN_WARNING "asgn2: Device already in use!\n");
        return -EBUSY;
    }    

    fin_reading = 0;
    atomic_inc(&asgn2_device.nprocs);
    printk(KERN_INFO "asgn2: Device Open: %d\n", atomic_read(&asgn2_device.nprocs));
    
    return 0;
}

/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn2_release (struct inode *inode, struct file *filp) {
    
    atomic_dec(&asgn2_device.nprocs);
    printk(KERN_INFO "asgn2: Device Release: %d\n", atomic_read(&asgn2_device.nprocs));

    return 0;
}

/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {

    size_t size_read = 0;                     /* size read from virtual disk in this function */
    size_t begin_offset;                      /* the offset from the beginning of a page to start reading */
    int begin_page_no = *f_pos / PAGE_SIZE;   /* the first page which contains the requested data */
    int curr_page_no = -1;                    /* the current page number */
    size_t curr_size_read;                    /* size read from the virtual disk in this round */
    size_t size_to_be_read;                   /* size to be read in the current round in while loop */
    size_t size_to_read;                      /* size left to read from kernel space */
    page_node *curr;                          /* the current node in the list */
    page_node *temp;                          /* temp node for if deleting first page */
    size_t total_to_read;

    if (fin_reading) {
        printk(KERN_INFO "asgn2: Already finished reading current session\n");
        return 0;
    }
    
    if (*f_pos > asgn2_device.data_size) {
        printk(KERN_WARNING "asgn2: Reading beyond device memory\n");
        return 0;
    }
    
    if (sessions.count == 0) {
        printk(KERN_WARNING "asgn2: Nothing to read!\n");
        return 0;
    }

    count = min(count, asgn2_device.data_size - sessions.read_offset);
    total_to_read = sessions.sizes[sessions.head];
    
    // Move head along by 1
    sessions.head = (sessions.head + 1) % MAX_SESSIONS;
    sessions.count--;
                          
    // loop through the list, reading the contents of each page
    list_for_each_entry_safe(curr, temp, &asgn2_device.mem_list, list) {
        // if we have reached the starting page to read from
        if (++curr_page_no == begin_page_no) {
            begin_offset = sessions.read_offset;
            size_to_read = min((int)(PAGE_SIZE - begin_offset), (int)(count - size_read));
            size_to_read = min(size_to_read, total_to_read);
            printk(KERN_INFO "asgn2: Reading from page %d with size %d\n", curr_page_no, size_to_read);
            size_to_be_read = copy_to_user(buf + size_read, page_address(curr->page) + begin_offset, size_to_read);
            printk(KERN_INFO "asgn2: Size left to read = %d\n", size_to_be_read);
            curr_size_read = size_to_read - size_to_be_read;
            size_to_read = size_to_be_read;
            size_read += curr_size_read;
            sessions.read_offset = (sessions.read_offset + curr_size_read) % PAGE_SIZE;

            // if read all from first page, remove it
            if (sessions.read_offset == 0) {
                __free_page(curr->page);
                list_del(&curr->list);
                asgn2_device.num_pages--;
                asgn2_device.data_size -= PAGE_SIZE;
                printk(KERN_INFO "asgn2: Removed the first page\n");
            }
            // if nothing else to read
            if (size_read == total_to_read) {
                printk(KERN_INFO "asgn2: Nothing left to read\n");
                fin_reading = 1;
                break;
            }
            // still something to read
            else {
                begin_page_no++;  // go to next page
                begin_offset = 0; // offset at start of page
                printk(KERN_INFO "asgn2: Read from the next page %d\n", begin_page_no);
            }
        }
    }

    *f_pos += size_read;
    
    printk(KERN_INFO "asgn2: size_read = %d\n", size_read);
    
    return size_read;
}

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn2_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {

    int nr = _IOC_NR(cmd);
    int new_nprocs;
    int result;

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
    .unlocked_ioctl = asgn2_ioctl,
    .open = asgn2_open,
    .release = asgn2_release,
};

/**
 * Initialise the module and create the master device
 */
int __init asgn2_init_module(void) {

    int result, index; 

    // init cbuf to 0
    cbuf.head = 0;
    cbuf.count = 0;
    for (index = 0; index < BUFFER_CAPACITY; index++) {
        cbuf.buffer[index] = 0;
    }

    // init sessions to 0
    sessions.head = 0;
    sessions.count = 0;
    sessions.read_offset = 0;
    sessions.write_offset = 0;
    for (index = 0; index < MAX_SESSIONS; index++) {
        sessions.sizes[index] = 0;
    }
    
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

    result = gpio_dummy_init();
    if (result < 0) {
        printk(KERN_WARNING "asgn2: %s failed to init gpio\n", MYDEV_NAME);
        goto fail_device;
    }
    
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
