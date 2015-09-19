MODULE_NAME = asgn2
# EXTRA_CFLAGS += -Werror

obj-m   := $(MODULE_NAME).o gpio.o

KDIR    := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)

all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *~

install: uninstall
	sudo insmod ./gpio.ko
	sudo insmod ./asgn2.ko
	sudo chmod 777 /dev/asgn2

uninstall:
	sudo dmesg -C
	-sudo rmmod asgn2
	-sudo rmmod gpio

mesg:
	dmesg | grep "asgn2:"
