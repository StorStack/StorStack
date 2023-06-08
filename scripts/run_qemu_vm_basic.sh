#!/bin/bash

source $(dirname "$0")/env.sh

sudo $QEMU_DIR/build/qemu-system-x86_64 -smp 8 -m 32G \
	-cpu host \
	-hda $WORK_DIR/disk.img \
	-hdb $WORK_DIR/share.img \
	-drive file=$WORK_DIR/ss.img,if=none,id=ss \
	-device nvme,serial=ss00,drive=ss \
	-enable-kvm \
	-boot order=dc \
	-drive file=/tmpss/test.img,if=none,id=test \
    -device nvme,serial=test01,drive=test \
	-vnc :1
