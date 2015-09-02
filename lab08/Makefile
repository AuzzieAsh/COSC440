### Automatic Makefile generation by 'genmake' script        ####
### Copyright, Jerry Cooperstein, coop@linuxfoundation.org 2/2003 - 1/2011 ####
### License: GPLv2 ###

obj-m +=  lab1_tasklet.o lab1_workqueue.o lab2_one_tasklet.o lab2_one_workqueue.o 

#lab2_one_thread.o lab3_one_tasklet_dynamic.o lab3_one_tasklet_improved.o lab3_one_thread_improved.o lab3_one_workqueue_dynamic.o lab4_all_tasklet.o lab4_all_thread.o lab4_all_workqueue.o lab5_all_tasklet_dynamic.o lab5_all_workqueue_dynamic.o lab6_all_threaded.o lab6_one_threaded.o lab7_uio.o lab8_uio_api.o

export KROOT=/lib/modules/$(shell uname -r)/build

allofit:  userprogs  modules
modules:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules
modules_install:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules_install
kernel_clean:
	@$(MAKE) -C $(KROOT) M=$(PWD) clean

userprogs:
	@$(MAKE) \
		CFLAGS="-O2 -Wall -pedantic" \
	 lab1_write lab4_all_getinterrupts lab8_uio_api_test

clean: kernel_clean
	rm -rf  lab1_write lab4_all_getinterrupts lab8_uio_api_test  Module.symvers modules.order *~
