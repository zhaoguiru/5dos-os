# Makefile for 5DOS-OS Kernel Module v0.4.3
# Target: Linux 6.8+ (proc_ops / timer_setup / kthread / kprobe API)

obj-m += 5dos_os.o
5dos_os-objs := 5dos-os.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod 5dos_os.ko

unload:
	sudo rmmod 5dos_os

reload: unload
	sleep 1
	sudo insmod 5dos_os.ko

status:
	cat /proc/5dos/status

top:
	cat /proc/5dos/top

schedmap:
	cat /proc/5dos/schedmap

enable:
	echo 1 | sudo tee /proc/5dos/control

disable:
	echo 0 | sudo tee /proc/5dos/control

hook-on:
	echo 1 | sudo tee /proc/5dos/hook

hook-off:
	echo 0 | sudo tee /proc/5dos/hook

.PHONY: all clean load unload reload status top schedmap enable disable hook-on hook-off
