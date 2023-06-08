#!/bin/bash

source $(dirname "$0")/env.sh

if [ -f $WORK_DIR/ubuntu-20.04.5-live-server-amd64.iso ]
then 
	echo "System CDROM exists."
else
	echo `sudo wget --no-check-certificate -P $WORK_DIR https://mirrors.tuna.tsinghua.edu.cn/ubuntu-releases/focal/ubuntu-20.04.5-live-server-amd64.iso`
fi
sudo $QEMU_DIR/build/qemu-img create -f raw $WORK_DIR/disk.img 100G
sudo $QEMU_DIR/build/qemu-img create -f raw $WORK_DIR/share.img 50G # 10G Not enough with kernel source code
sudo mkfs ext4 -F $WORK_DIR/share.img
sudo $QEMU_DIR/build/qemu-img create -f raw $WORK_DIR/ss.img 50G

sudo $QEMU_DIR/build/qemu-system-x86_64 -smp 8 -m 8G \
	-cpu host \
	-hda $WORK_DIR/disk.img \
	-hdb $WORK_DIR/share.img \
	-drive file=$WORK_DIR/ss.img,if=none,id=ss \
	-device nvme,serial=ss00,drive=ss \
    -cdrom $WORK_DIR/ubuntu-20.04.5-live-server-amd64.iso \
	-enable-kvm \
	-boot order=dc \
	# -vnc :0
