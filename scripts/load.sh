#!/bin/bash
set -e
sudo insmod src/5dos-os.ko
echo 1 | sudo tee /proc/5dos/control
echo 1 | sudo tee /proc/5dos/hook
echo "5DOS-OS loaded, scheduling and hooks enabled."
