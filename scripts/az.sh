# 1. 把目录拷进内核源码树
mkdir -p /usr/src/linux/kernel/5dos
cp /mnt/hgfs/AI/5DOS/os/5dos-os/src/* /usr/src/linux/kernel/5dos/

# 2. 在 kernel/Makefile 添加
echo 'obj-$(CONFIG_5DOS_OS) += 5dos/' >> /usr/src/linux/kernel/Makefile

# 3. 在 kernel/Kconfig 添加
echo 'source "kernel/5dos/Kconfig"' >> /usr/src/linux/kernel/Kconfig

# 4. 配置 & 编译
make menuconfig   # 选中 5DOS_OS
make -j$(nproc)
