/* **************** LDD:1.0 s_12/semaphore3.c **************** */
/*
 * The code herein is: Copyright Jerry Cooperstein, 2009
 *
 * This Copyright is retained for the purpose of protecting free
 * redistribution of source.
 *
 *     URL:    http://www.coopj.com
 *     email:  coop@coopj.com
 *
 * The primary maintainer for this code is Jerry Cooperstein
 * The CONTRIBUTORS file (distributed with this
 * file) lists those known to have contributed to the source.
 *
 * This code is distributed under Version 2 of the GNU General Public
 * License, which you should have received with the source.
 *
 */
/*
 * Semaphore Contention
 *
 * second and third module to test semaphores
 @*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <asm/atomic.h>
#include <linux/errno.h>

extern struct semaphore my_semaphore;

static char *modname = __stringify(KBUILD_BASENAME);

static int __init my_init(void)
{
	printk(KERN_INFO "Trying to load module %s\n", modname);

	if (down_interruptible(&my_semaphore)) {
		printk(KERN_INFO "semaphore %s woken by a signal\n", modname);
		return -ERESTARTSYS;
	}

	printk(KERN_INFO "\n%s semaphore awakening\n", modname);

	return 0;
}

static void __exit my_exit(void)
{
	up(&my_semaphore);

	printk(KERN_INFO "\n%s semaphore unlocked\n", modname);
}

module_init(my_init);
module_exit(my_exit);

MODULE_AUTHOR("Ashley Manson");
MODULE_DESCRIPTION("LDD:1.0 s_12/semaphore3.c");
MODULE_LICENSE("GPL v2");
