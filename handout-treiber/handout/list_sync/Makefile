# Makefile:
#

MODULENAME1 	:= list_not_safe
MODULENAME2 	:= list_spinlock
MODULENAME3 	:= list_rwlock
MODULENAME4 	:= list_rcu
MODULENAME5 	:= list_mutex
MODULENAME6 	:= list_rtmutex

# KDIR		:= /home/ak/inst/linux-3.6.1-wut
KDIR		:= /home/ak/inst-wega/linux-3.12.31-rt45
# KDIR		:= /home/ak/inst/linux-3.6.1-rt1-wut
# KDIR		:= /home/ak/inst/linux-3.2.17-rt12-wut
PWD		:= $(shell pwd)

CC		:= ${CROSS_COMPILE}gcc -Wall

ifeq ($(KERNELRELEASE),)
all:
	$(MAKE) V=0 -C $(KDIR) SUBDIRS=$(PWD) modules
else
  obj-m	:= $(MODULENAME1).o $(MODULENAME2).o $(MODULENAME3).o $(MODULENAME4).o $(MODULENAME5).o $(MODULENAME6).o
endif

clean:
	-rm -rf *.o *.ko modules.order Module.symvers *.mod.c .*.cmd .tmp_versions


