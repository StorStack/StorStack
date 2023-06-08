#!/bin/bash

source $(dirname "$0")/env.sh

sudo mkdir $WORK_DIR/mnt 

echo "mounting shared image"
sudo mount -t ext4 $WORK_DIR/share.img $WORK_DIR/mnt 
# sudo cp -R $KERNAL_DIR $WORK_DIR/mnt 
sudo mkdir -p $WORK_DIR/mnt/workspace 

# TODO: kernel make install minimum movement

echo "copying linux kernel..."
# sudo cp $KERNAL_DIR/arch/x86/boot/bzImage $WORK_DIR/mnt/workspace 
# sudo cp $KERNAL_DIR/System.map $WORK_DIR/mnt/workspace 


sudo cp $KERNAL_DIR/drivers/storstack/klibss.ko $WORK_DIR/mnt/workspace
sudo cp -u -R $KERNAL_DIR $WORK_DIR/mnt

echo "copying storstack codes..."
sudo cp -u -R $ULIB_DIR $WORK_DIR/mnt
sudo cp -u -R $UAPP_DIR $WORK_DIR/mnt
sudo cp -u -R $SCRIPT_DIR $WORK_DIR/mnt 
sudo cp -u -R $MISC_DIR $WORK_DIR/mnt 
sudo umount $WORK_DIR/mnt

echo "done."