
clean:
	-rm -f *.o *.ko modules.order Module.symvers *.mod.c .*.cmd
	-rm -rf .tmp_versions

copy:
	#scp morse.ko root@192.168.1.125:
	scp sendx root@192.168.1.125:

all:
	$(CROSS_COMPILE)gcc -O0 -g sendx.c -o sendx 

