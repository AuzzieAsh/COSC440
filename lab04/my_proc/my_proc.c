/*
  File: my_proc.c
  Author: Ashley Manson
*/

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>

#define NODE "my_new_proc"

static int a, b, c;
static struct proc_dir_entry *my_proc;

static int
my_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data) {
	int res;
    *eof = 1;
    res = a+b*c;
	return sprintf(page, "[a+b*c] %d + %d * %d = %d\n", a,b,c,res);
}

static int
my_proc_write(struct file *file, const char __user * buffer, unsigned long count, void *data) {
	char *str;
	str = kmalloc((size_t) count, GFP_KERNEL);
	if (copy_from_user(str, buffer, count)) {
		kfree(str);
		return -EFAULT;
	}
	if (3 != sscanf(str, "%d %d %d", &a, &b, &c)) {
        printk(KERN_ERR "Nope, you gotta give me three integers\n love %s", NODE);
        return -1;
    }
	printk(KERN_INFO "a, b and c have been set to %d %d %d\n", a,b,c);
	kfree(str);
	return count;
}

static int __init my_init(void) {
    my_proc = create_proc_entry(NODE, 666, NULL);
    if (!my_proc) {
        printk(KERN_ERR "I failed to make %s\n", NODE);
        return -1;
    }
    printk(KERN_INFO "I created %s\n", NODE);
    my_proc->read_proc = my_proc_read;
    my_proc->write_proc = my_proc_write;
    a = 0;
    b = 0;
    c = 0;
	return 0;
}

static void __exit my_exit(void) {
    if (my_proc) remove_proc_entry(NODE, NULL);
}

module_init(my_init);
module_exit(my_exit);

MODULE_AUTHOR("Ashley Manson");
MODULE_DESCRIPTION("lab04/my_proc_thing.c");
MODULE_LICENSE("GPL v2");
