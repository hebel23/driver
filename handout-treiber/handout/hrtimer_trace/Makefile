# set your module name here
# provide the following files
#
# MODULENAME.c	the driver
# MODULENAME.h  the include file
# MODULENAME_test.c	the driver test program
# MODULENAME_test.sh	the driver test script
#
MODULENAME 	:= hrt
### should not need to change stuff below ######################


# KDIR		:= /lib/modules/$(shell uname -r)/build
#KDIR		:= /root/sandbox/linux-2.6.33.7-rt29
# KDIR		:= /root/inst-wut/linux-2.6.33.7-rt29-wut
# KDIR		:= /usr/src/kernels/linux-2.6.33.7.2-rt30/
KDIR		:= /home/ak/inst-wega/linux-3.12.19-rt30
PWD		:= $(shell pwd)

CC		:= ${CROSS_COMPILE}gcc -Wall

ifeq ($(KERNELRELEASE),)
all:	$(MODULENAME)_test
all:
	cp ./jitter.h $(KDIR)/include/trace/events/jitter.h
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
#	$(MAKE) V=1 -C $(KDIR) SUBDIRS=$(PWD) modules
else
  obj-m	:= $(MODULENAME).o
endif

$(MODULENAME)_test: $(MODULENAME)_test.c
	$(CC) -g -o $(MODULENAME)_test $(MODULENAME)_test.c


load:
	insmod ./$(MODULENAME).ko

unload:
	rmmod $(MODULENAME)

test: all
	./$(MODULENAME)_test.sh

clean:
	-rm -f *.o *.ko modules.order Module.symvers *.mod.c .*.cmd $(MODULENAME)_test
	-rm -rf .tmp_versions

