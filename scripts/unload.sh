#!/bin/bash
set -e
echo 0 | sudo tee /proc/5dos/control
echo 0 | sudo tee /proc/5dos/hook
sudo rmmod 5dos-os
echo "5DOS-OS unloaded."
