#!/bin/bash

source $(dirname "$0")/env.sh

qemu-system-x86_64 -smp 8 -m 32G \
	-cpu host \
	-hda $WORK_DIR/nova.img \
	-hdb $WORK_DIR/share.img \
	-enable-kvm \
	-monitor stdio