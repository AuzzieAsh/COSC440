/**
 * File: asgn1.c
 * Date: 13/03/2011
 * Modified: 01/09/2015
 * Author: Ashley Manson 
 * Version: 1.0
 *
 * This is a module which serves as a virtual ramdisk which disk size is
 * limited by the amount of memory available and serves as the requirement for
 * COSC440 assignment 1 in 2015.
 *
 * Note: multiple devices and concurrent modules are not supported in this
 *       version.
 */

/*
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

    int page_num = 0;
    page_node *curr, *tmp;

    printk(KERN_INFO "asgn1: free_memory_pages called\n");

    // loop through the list, while freeing all the pages
    list_for_each_entry_safe(curr, tmp, &asgn1_device.mem_list, list) {
        if (curr != NULL) {
            printk(KERN_INFO "asgn1: Freeing memory page %d\n", page_num++);
            __free_page(curr->page);
            list_del(&curr->list);
            kfree(curr);
        }
    }

    // resey data_size and num_pages
    asgn1_device.data_size = 0;
    asgn1_device.num_pages = 0;
    
    printk(KERN_INFO "asgn1: free_memory_pages finished\n");
}

/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn1_open(struct inode *inode, struct file *filp) {

    int num_procs = atomic_read(&asgn1_device.nprocs);
    int max_num_procs = atomic_read(&asgn1_device.max_nprocs);

    printk(KERN_INFO "asgn1: asgn1_open called\n");
    
    if (num_procs >= max_num_procs) {
        printk(KERN_WARNING "asgn1: Device already in use!\n");
        return -EBUSY;
    }
    
    atomic_inc(&asgn1_device.nprocs);
    printk(KERN_INFO "asgn1: Process count incremented to %d\n", atomic_read(&asgn1_device.nprocs));

    // If opened in write-only
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        printk(KERN_INFO "asgn1: Opened in write-only\n");
        free_memory_pages();
    }
    
    printk(KERN_INFO "asgn1: asgn1_open finished\n");

    return 0;
}

/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn1_release (struct inode *inode, struct file *filp) {

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
    int curr_page_no = -1;                    /* the current page number */
    size_t curr_size_read;                    /* size read from the virtual disk in this round */
    size_t size_to_be_read;                   /* size to be read in the current round in while loop */
    size_t size_to_read;                      /* size left to read from kernel space */
    size_t size_from_pages;                   /* maximum size to read from all pages */
    page_node *curr;                          /* the current node in the list */

    printk(KERN_INFO "asgn1: asgn1_read called\n");
    printk(KERN_INFO "asgn1: Number of pages %d\n", asgn1_device.num_pages);

    if (*f_pos > asgn1_device.data_size) {
        printk(KERN_WARNING "asgn1: f_pos (%d) > data_size (%d)\n", (int)*f_pos, (int)asgn1_device.data_size);
        return 0;
    }

    size_from_pages = min(count, asgn1_device.data_size - (size_t)*f_pos);

    // loop through the list, reading the contents of each page
    list_for_each_entry(curr, &asgn1_device.mem_list, list) {
        // if we have reached the starting page to read from
        if (++curr_page_no == begin_page_no) {
            size_to_read = min((int)(PAGE_SIZE - begin_offset), (int)(size_from_pages - size_read));
            printk(KERN_INFO "asgn1: Reading from page %d with size %d\n", curr_page_no, size_to_read);
            size_to_be_read = copy_to_user(buf + size_read, page_address(curr->page) + begin_offset, size_to_read);
            printk(KERN_INFO "asgn1: Size left to read = %d\n", size_to_be_read);
            curr_size_read = size_to_read - size_to_be_read;
            size_to_read = size_to_be_read;
            size_read += curr_size_read;
            printk(KERN_INFO "asgn1: size_from_pages %d, size_read %d\n", size_from_pages, size_read);
            // if still more to read
            if (size_from_pages != size_read) {
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

    *f_pos += size_read;
    
    printk(KERN_INFO "asgn1: size_read = %d\n", size_read);
    
    printk(KERN_INFO "asgn1: asgn1_read finished\n");
    
    return size_read;
}

static loff_t asgn1_lseek (struct file *file, loff_t offset, int cmd) {
    
    loff_t testpos;
    size_t buffer_size = asgn1_device.num_pages * PAGE_SIZE;

    printk(KERN_INFO "asgn1: asgn1_leek called\n");
    
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
    
    printk(KERN_INFO "asgn1: Seeking to pos=%ld\n", (long)testpos);

    printk(KERN_INFO "asgn1: asgn1_leek finished\n");
    
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
    int curr_page_no = -1;                    /* the current page number */
    size_t curr_size_written;                 /* size written to virtual disk in this round */
    size_t size_to_be_written;                /* size to be read in the current round in while loop */
    size_t size_to_write = count;             /* size left to copy over from user space */
    page_node *curr;                          /* the current node in the list */

    printk(KERN_INFO "asgn1: asgn1_write called\n");
    printk(KERN_INFO "asgn1: *f_pos + count = %d\n", (int)(*f_pos + count));
      
    // add pages if necessary
    while (asgn1_device.num_pages * PAGE_SIZE < *f_pos + count) {
        curr = kmalloc(sizeof(page_node), GFP_KERNEL);
        if (curr != NULL) {
            curr->page = alloc_page(GFP_KERNEL);
            if (curr->page == NULL) {
                printk(KERN_WARNING "asgn1: Page allocation failed!\n");
                return size_written;
            }
            list_add_tail(&curr->list, &asgn1_device.mem_list);
            asgn1_device.num_pages++;
            printk(KERN_INFO "asgn1: Added pages to list: %d\n", asgn1_device.num_pages);
        }
        else {
            printk(KERN_WARNING "asgn1: Couldn't add pages to list!\n");
            return size_written;
        }
    }

    // loop through the list, writing to each page
    list_for_each_entry(curr, &asgn1_device.mem_list, list) {
        // if we have reached the starting page to write
        if (++curr_page_no == begin_page_no) {
            size_to_write = min((int)(PAGE_SIZE - begin_offset), (int)(count - size_written));
            printk(KERN_INFO "asgn1: Writing to page %d with size %d\n", curr_page_no, size_to_write);
            size_to_be_written = copy_from_user(page_address(curr->page) + begin_offset, buf + size_written, size_to_write);
            printk(KERN_INFO "asgn1: Size left to write = %d\n", size_to_be_written);
            curr_size_written = size_to_write - size_to_be_written;
            size_to_write = size_to_be_written;
            size_written += curr_size_written;
            printk(KERN_INFO "asgn1: count %d, size_written %d\n", count, size_written);
            // if still more to write
            if (size_written != count) {
                begin_page_no++;  // go to next page
                begin_offset = 0; // offset at start of page
                printk(KERN_INFO "asgn1: Write to the next page %d\n", begin_page_no);
            }
            // nothing else to write
            else {
                printk(KERN_INFO "asgn1: Nothing left to write\n");
                break;
            }
        }
    }

    *f_pos += size_written;
    
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

    int nr = _IOC_NR(cmd);
    int new_nprocs;
    int result;

    printk(KERN_INFO "asgn1: asgn1_ioctl called\n");

    // check if cmd is for this device
    if (_IOC_TYPE(cmd) != MYIOC_TYPE) {
        printk(KERN_WARNING "asgn1: Got invalid case cmd = %d\n", cmd);
        return -EINVAL;
    }
    
    switch(nr) {
    case SET_NPROC_OP:
        result = access_ok(VERIFY_READ, arg, sizeof(int));
        if (!result) {
            printk(KERN_WARNING "asgn1: ioctl argument is not valid!\n");
            return -EFAULT;
        }
        result = get_user(new_nprocs, (int *)arg);
        if (new_nprocs < 0) {
            printk(KERN_WARNING "asgn1: user tried to set max_nprocs to less than 0!\n");
            return -EINVAL;
        }
        atomic_set(&asgn1_device.max_nprocs, new_nprocs);
        break;
    default:
        return -ENOTTY;
    }

    printk(KERN_INFO "asgn1: asgn1_ioctl finished\n");
    
    return 0;
}

/**
 * Displays information about current status of the module,
 * which helps debugging.
 */
int asgn1_read_procmem(char *buf, char **start, off_t offset, int count, int *eof, void *data) {

    *eof = 1;
    return snprintf(buf, count, "nprocs %d, max_nprocs %d\nnum_pages %d, data_size %d\n",
                      atomic_read(&asgn1_device.nprocs),
                      atomic_read(&asgn1_device.max_nprocs),
                      asgn1_device.num_pages,
                      asgn1_device.data_size);
}

static int asgn1_mmap (struct file *filp, struct vm_area_struct *vma) {

    unsigned long pfn = vma->vm_start / PAGE_SIZE;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long len = vma->vm_end - vma->vm_start;
    unsigned long ramdisk_size = asgn1_device.num_pages * PAGE_SIZE;
    page_node *curr;
    unsigned long index = 0;

    printk(KERN_INFO "asgn1: asgn1_mmap called\n");
    
    if (offset > ramdisk_size || len > ramdisk_size) {
        printk(KERN_WARNING "asgn1: offset or len are invalid!\n");
        return -EINVAL;
    }

    // loop through the list, remapping the requested pages
    list_for_each_entry(curr, &asgn1_device.mem_list, list) {
        if (index == pfn) {
            printk(KERN_INFO "asgn1: Remaping pages from %ld to %ld\n", vma->vm_start, vma->vm_end);
            if (remap_pfn_range(vma, vma->vm_start, offset, len, vma->vm_page_prot)) {
                printk(KERN_WARNING "asgn1: Failed to remap pages!\n");
                return -EAGAIN;
            }
            break;
        }
        index++;
    }

    printk(KERN_INFO "asgn1: asgn1_mmap finished\n");
    
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

    atomic_set(&asgn1_device.nprocs, 0);
    atomic_set(&asgn1_device.max_nprocs, 1);
    result = alloc_chrdev_region(&asgn1_device.dev, asgn1_minor, asgn1_dev_count, MYDEV_NAME);
    if (result < 0)
        goto fail_device;
    asgn1_device.cdev = cdev_alloc();
    asgn1_device.cdev->ops = &asgn1_fops;
    asgn1_device.cdev->owner = asgn1_fops.owner;
    result = cdev_add(asgn1_device.cdev, asgn1_device.dev, asgn1_dev_count);
    if (result < 0)
        goto fail_device;
    INIT_LIST_HEAD(&asgn1_device.mem_list);
    asgn1_proc = create_proc_entry(MYDEV_NAME, 777, NULL);
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

fail_device:
    class_destroy(asgn1_device.class);


    free_memory_pages();
    if (asgn1_device.dev)
        unregister_chrdev_region(asgn1_device.dev, 1);
    if (asgn1_device.class)
        class_destroy(asgn1_device.class);
    if (asgn1_proc)
        remove_proc_entry(MYDEV_NAME, NULL);
    if (asgn1_device.cdev)
        cdev_del(asgn1_device.cdev);
    
    return result;
}

/**
 * Finalise the module
 */
void __exit asgn1_exit_module(void) {
    
    device_destroy(asgn1_device.class, asgn1_device.dev);
    class_destroy(asgn1_device.class);
    
    printk(KERN_WARNING "asgn1: cleaned up udev entry\n");

    free_memory_pages();
    unregister_chrdev_region(asgn1_device.dev, 1);
    remove_proc_entry(MYDEV_NAME, NULL);
    cdev_del(asgn1_device.cdev);

    printk(KERN_WARNING "asgn1: Good bye from %s\n", MYDEV_NAME);
}

module_init(asgn1_init_module);
module_exit(asgn1_exit_module);
