obj-m	+= wait_event.o

KDIR	:= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

EXTRA_CFLAGS	+= -I$(src)/../../include

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	gcc -W -Wall read.c -o read
	gcc -W -Wall write.c -o write

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
	rm -f modules.order
	rm -f *~
	rm -f read
	rm -f write
