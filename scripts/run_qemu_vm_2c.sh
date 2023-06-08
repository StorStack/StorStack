#!/bin/bash

source $(dirname "$0")/env.sh

sudo $QEMU_DIR/build/qemu-system-x86_64 -smp 8 -m 32G \
	-cpu host \
	-object iothread,id=t0 \
	-object iothread,id=t1 \
	-hda $WORK_DIR/disk.img \
	-hdb $WORK_DIR/share.img \
	-drive file=$WORK_DIR/ss.img,if=none,id=ss \
	-device nvme,serial=ss00,drive=ss,ctrl_cores=2,len-core_threads=2,core_threads[0]=t0,core_threads[1]=t1,bind_cpu_from=38,mdts=12 \
	-drive file=/tmpss/norm.img,if=none,id=norm \
    -device nvme,serial=norm01,drive=norm \
	-enable-kvm \
	-monitor stdio -vnc :1
	# -object iothread,id=t1 \
	# -device nvme,serial=ss00,drive=ss,ctrl_cores=2,len-core_threads=2,core_threads[0]=t0,core_threads[1]=t1,len-bind_cpus=2,bind_cpus[0]=4,bind_cpus[1]=6,mdts=12 \
	# -machine q35,accel=kvm,kernel-irqchip=split \
	# -device intel-iommu,intremap=on \
	# -vnc :0
	# -qmp unix:$QEMU_DIR/scripts/qmp/qmp-sock,server,wait=off 
	# -name ssvm,debug-threads=on
	# -cdrom $WORK_DIR/ubuntu-20.04.5-live-server-amd64.iso

	 #,len-bind_cpus=4,bind_cpus[0]=3,bind_cpus[1]=4,bind_cpus[2]=5,mdts=12 \
