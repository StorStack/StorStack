sudo mkfs.ext4 /dev/pmem0
sudo mount -t ext4 -o dax /dev/pmem0 /mnt/pmem