void free_memory_pages(void);
/**
 * Loop through the entire page list {
 *   if (node has a page) {
 *     free the page
 *   }
 *   remove the node from the page list
 *   free the node
 * }
 * reset device data size, and num_pages
 */
 
int asgn1_open(struct inode *inode, struct file *filp);
/**
 * Increment process count, if exceeds max_nprocs, return -EBUSY
 * if opened in write-only mode, free all memory pages
 */

int asgn1_release (struct inode *inode, struct file *filp);
/**
 * decrement process count
 */

ssize_t asgn1_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
/**
 * check f_pos, if beyond data_size, return 0
 * 
 * Traverse the list, once the first requested page is reached,
 *   - use copy_to_user to copy the data to the user-space buf page by page
 *   - you also need to work out the start / end offset within a page
 *   - Also needs to handle the situation where copy_to_user copy less
 *       data than requested, and
 *       copy_to_user should be called again to copy the rest of the
 *       unprocessed data, and the second and subsequent calls still
 *       need to check whether copy_to_user copies all data requested.
 *       This is best done by a while / do-while loop.
 *
 * if end of data area of ramdisk reached before copying the requested
 *   return the size copied to the user space so far
 */
 
static loff_t asgn1_lseek (struct file *file, loff_t offset, int cmd);
/**
 * set testpos according to the command
 * if testpos larger than buffer_size, set testpos to buffer_size
 * if testpos smaller than 0, set testpos to 0
 * set file->f_pos to testpos
 */

ssize_t asgn1_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
/**
 * Traverse the list until the first page reached, and add nodes if necessary
 *
 * Then write the data page by page, remember to handle the situation
 *   when copy_from_user() writes less than the amount you requested.
 *   a while loop / do-while loop is recommended to handle this situation. 
 */

long asgn1_ioctl (struct file *filp, unsigned cmd, unsigned long arg); 
/**
 * check whether cmd is for our device, if not for us, return -EINVAL 
 *
 * get command, and if command is SET_NPROC_OP, then get the data, and
 * set max_nprocs accordingly, don't forget to check validity of the 
 * value before setting max_nprocs
 */

int asgn1_read_procmem(char *buf, char **start, off_t offset, int count, int *eof, void *data);
/**
 * use snprintf to print some info to buf, up to size count
 * set eof
 */
static int asgn1_mmap (struct file *filp, struct vm_area_struct *vma);
/**
 * check offset and len
 *
 * loop through the entire page list, once the first requested page
 *   reached, add each page with remap_pfn_range one by one
 *   up to the last requested page
 */

int __init asgn1_init_module(void);
/**
 * set nprocs and max_nprocs of the device
 *
 * allocate major number
 * allocate cdev, and set ops and owner field 
 * add cdev
 * initialize the page list
 * create proc entries
 */
                                                                                                                                                    
/**
 * cleanup code called when any of the initialization steps fail
 */

void __exit asgn1_exit_module(void);
/**
 * free all pages in the page list 
 * cleanup in reverse order
 */
