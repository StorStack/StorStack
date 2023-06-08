## Filebench auto scripts and workloads

### Check your Disk S.M.A.R.T.

```shell
sudo apt install smartctl -y
smartctl -a /dev/nvme0
```

### Config

old:
    host 48g(+extra pmem 16g)
    vm 24g(+extra pmem 12g)

new:
    host pmem 16g
    host os 48g
        vm pmem 16g
        vm os 16g

### mount tmpfs on /tmp

```shell
    mount -t tmpfs -o size=10g tmpfs /tmp
```

### create img file (raw)

```bash

sudo ./qemu/build/qemu-img create -f raw /tmp/test.img 20G

```

### qemu nvme device

```conf
    -drive file=$WORK_DIR/test.img,if=none,id=test \
    -device nvme,serial=test1,drive=test \
```

### set mem as pmem

1. Go to the 4.4.4 kernel, then type the command: sudo make menuconfig
2. After step 1, the setup screen will appear, type ? (shortcut key for search), and then type dax in the input box
3. select the first BLK_DEV_RAM_DAX (corresponding to the number, press the number to enter the new interface), and then set the number of ram disk to 3 in the ext4-dax interface, and select support dax on it.
4. During this time, you also need to set the system to support mounting block devices, i.e. to divide memory in the way of memmap. Specifically 1. select process type and feature in the setup screen, select support_non_stand_nvdimms. 2. select nv2dmm in device drivers. 3. select direct-access in file system. (The last two steps are not available.)
1. save the settings and recompile the kernel (the previous step means that ext4-dax is supported)
2. After compiling, reboot the computer, select kernel 4.4.4, then press e to enter the edit kernel boot parameters screen, find the penultimate line, add memmap=2g!10g (indicating that a 2g space is divided in memory, starting from the 10g location)
3. After booting, type sudo mkfs.ext4 /dev/pmem0 in the terminal (i.e. format the 2g space you allocated as ext4-dax management space)
4. mount the filesystem ext4-dax. i.e. sudo mount -o dax /dev/pmem0 /mnt/pmem
5. df -h to see if it is mounted.

### normal

**-enable-kvm**

- in vm pmem e4dax - /mnt/pmem
    grub and memmap set

```shell
    mount -t ext4-dax /dev/pmem /mnt/pmem
```

- in vm tmpfs - /tmp

```shell
    mount -t tmpfs -o size=2g tmpfs /tmp
```

- out vm img blk ext4 - /mnt/shared

```shell
    mount -t ext4 /dev/sdb /mnt/shared
```

- out vm img blk tmpfs - /mnt/shared

```shell
    mount -t ext4 /dev/sdc /mnt/shared
```

- out vm img nvme ext4 - /mnt/nvme_shared

```shell
    mount -t ext4 /dev/nvme0n1p1 /mnt/nvme_shared
```

- out vm img nvme tmpfs - /mnt/nvme_shared

```shell
    mount -t ext4 /dev/nvme0n1p1 /mnt/nvme_shared
```

