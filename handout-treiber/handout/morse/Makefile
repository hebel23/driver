# Makefile: morse
#

#MODULENAME 	:= morse
MODULENAME 	:= morse-dt

KDIR		:= /opt/PHYTEC_BSPs/phyCORE-AM335x-PD12.1.0/platform-phyCORE-AM335x/build-target/linux-3.2
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
	scp morse-dt.ko root@192.168.0.81:

