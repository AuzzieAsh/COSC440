MODULE_NAME = asgn2
# EXTRA_CFLAGS += -Werror

obj-m   := $(MODULE_NAME).o
asgn2-objs := asgn.o gpio.o

KDIR    := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)

all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *~

install: uninstall
	sudo insmod ./$(MODULE_NAME).ko
	sudo chown pi:pi /dev/$(MODULE_NAME)
	sudo chmod 777 /dev/$(MODULE_NAME)

uninstall:
	sudo dmesg -C
	-sudo rmmod $(MODULE_NAME)
