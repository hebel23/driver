# Makefile: srf04-char
#

MODULENAME 	:= srf04-char

KDIR		:= /home/nutzer31/treiber/linux-4.14.15

PWD		:= $(shell pwd)

CC		:= ${CROSS_COMPILE}gcc -Wall

ifeq ($(KERNELRELEASE),)
all:
	$(MAKE) V=0 -C $(KDIR) SUBDIRS=$(PWD) modules
else
  obj-m	:= $(MODULENAME).o
endif


clean:
	-rm -f *.o *.ko modules.order Module.symvers *.mod.c .*.cmd
	-rm -rf .tmp_versions

copy:
	scp srf04-char.ko root@192.168.1.125:

