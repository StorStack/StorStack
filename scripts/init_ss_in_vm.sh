#!/bin/bash

sudo sysctl -w kernel.randomize_va_space=0
sudo nvme format /dev/nvme0 --namespace-id=1 -lbaf=4

sudo insmod /mnt/shared/workspace/klibss.ko
sudo /mnt/shared/ulibss/spdk/scripts/setup.sh
sudo /mnt/shared/ulibss/utrayss
