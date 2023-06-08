#!/bin/bash

source $(dirname "$0")/env.sh

# cd $KERNAL_DIR
# sudo make modules_install -j$MJOBS
# sudo make install -j$MJOBS
sudo installkernel 5.15.55-ss $WORK_DIR/bzImage $WORK_DIR/System.map /boot

sudo mkinitramfs -o /boot/initrd.img-5.15.55-ss
sudo update-grub