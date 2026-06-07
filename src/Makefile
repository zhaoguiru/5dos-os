# Makefile for 5DOS-OS Kernel Module
# Dual-mode: External Module (.ko) OR Built-in (kernel tree)
# Target: Linux 6.8+

# ============================================
# 模式1：放入内核源码树编译（built-in / built-in module）
# 用法：将本目录复制到 kernel/5dos/，并在 kernel/Makefile 添加：
#       obj-$(CONFIG_5DOS_OS) += 5dos/
# ============================================
ifneq ($(KERNELRELEASE),)

obj-$(CONFIG_5DOS_OS) += 5dos_os.o
5dos_os-y := 5dos-os.o

# ============================================
# 模式2：外部模块编译（开发调试，默认）
# 用法：直接 make / make load / make unload
# ============================================
else

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

endif
