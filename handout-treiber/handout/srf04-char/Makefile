# Makefile: srf04-char
#

MODULENAME 	:= srf04-char

#KDIR		:= /home/ak/inst-wega/linux-4.8/
KDIR		:= /project/yocto/wega/tmp/work/phyboard_wega_am335x_1-poky-linux-gnueabi/linux-bienenwaage/4.14.1-r0/build

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
	scp srf04-char.ko root@192.168.0.106:

