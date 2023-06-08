# StorStack Readme

[TOC]

## "Device side" Dependencies

### env PATH

Assume you are in `storstack` directory.
Then run:

```bash
export BASE_DIR=$PWD
export SCRIPT_DIR=$BASE_DIR/"scripts"
export KERNAL_DIR=$BASE_DIR/"linux-5.15"
export QEMU_DIR=$BASE_DIR/"qemu"
export ULIB_DIR=$BASE_DIR/"ulibss"
export UAPP_DIR=$BASE_DIR/"uapps"
export WORK_DIR=$BASE_DIR/"workspace"
```

### Dependencies

```bash
$SCRIPT_DIR/install_deps.sh
```

### Build customized kernel

```bash
sudo $SCRIPT_DIR/build_kernel.sh
```

### Build QEMU

```bash
sudo $SCRIPT_DIR/build_qemu.sh
```

### Install Ubuntu 20.04 LTS

```bash
cd $BASE_DIR
$SCRIPT_DIR/install_qemu_vm.sh
```

The step will download `Ubuntu Desktop 20.04.5 LTS` from a fixed mirror site, and start a VM.

Then you should connect to VM according to console infomation via VNR to finish Ubuntu installment.

**Attention**: VM Operating System must be installed in `disk.img`, not in `share.img` or `ss.img`.

When console says `You need to eject your CDROM and press ENTER`, kill QEMU process (`Ctrl-C`) and re-run VM, enter desktop environment:

```bash
$SCRIPT_DIR/run_qemu_vm.sh
```

Do not forget to mount `share.img` virtual device to VM again:

```bash
sudo mount -t ext4 /dev/sdb /mnt/share
```

### Move code and binaries to `share.img`

```bash

sudo $SCRIPT_DIR/move_to_vm.sh

```

## "Host side" Setup

### Prepare QEMU VM Linux System

**Steps below are done in VM rather than Host Machine (Device side)!**

#### Mount `share.img`

Mount `share.img` virtual device to VM:

```bash
sudo mount -t ext4 /dev/sdb /mnt/share
```

#### Install dependencies in VM

```bash
sudo /mnt/share/scripts/install_vm_deps.sh
```

#### Install new kernel in VM

```bash
cd /mnt/share/linux-5.15
sudo make modules_install
sudo make install
```

`update-grub` is optional because after `make install` the grub will be updated automatically.

```bash
update-grub
```

#### Install DPDK

Use Bash script:

```bash
sudo $BASE_DIR/scripts/install_spdk.sh
```

**or** manually:

```bash
cd /mnt/share/ulibss/spdk/dpdk
sudo meson build
sudo ninja -C build -j8
cd build
sudo ninja install
sudo ldconfig
```

Check DPDK status:

```bash
pkg-config --cflags libdpdk
pkg-config --libs libdpdk
```

#### Install SPDK

Setup SPDK dependencies. The step has been written in `scripts/install_vm_deps.sh`, which was executed before.

So it is optional:

```bash
sudo $BASE_DIR/ulibss/spdk/scripts/pkgdep.sh --all
```

Use Bash script:

```bash
sudo $BASE_DIR/scripts/install_spdk.sh
```

**or** manually:

```bash
cd /mnt/share/ulibss/spdk
./configure --with-shared
sudo make -j8
sudo make install
```

Then run `setup.sh` to test SPDK:

```bash
sudo $BASE_DIR/ulibss/spdk/scripts/setup.sh
```

### Build ULIBs and UAPPs

Normally, ULIBs and UAPPs are not as big as Linux Kernel, so compile in VM is acceptable:

```bash
$SCRIPT_DIR/build_uapps.sh
```

### Build filebench

```shell
cd filebench
libtoolize
aclocal
autoheader
automake --add-missing
autoconf
./configure
make install

sudo sysctl -w kernel.randomize_va_space=0 # close ASLR
```

### Things Should Run Every Time After VM Start

```bash
sudo mount -t ext4 /dev/sdb /mnt/share
cd /mnt/share
sudo ./scripts/init_ss_in_vm.sh
```