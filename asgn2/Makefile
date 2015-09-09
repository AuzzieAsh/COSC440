MODULE_NAME = asgn2
# EXTRA_CFLAGS += -Werror


obj-m   := $(MODULE_NAME).o


KDIR    := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)



all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *~

install: uninstall
	sudo insmod ./asgn2.ko
	sudo chmod 777 /dev/asgn2

uninstall:
	sudo dmesg -c
	-sudo rmmod asgn2

mesg:
	dmesg | grep "asgn2:"
