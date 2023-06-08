#!/usr/bin/env bash

source $(dirname "$0")/env.sh

# sudo apt install kpartx -y
sudo apt install build-essential zlib1g-dev pkg-config libglib2.0-dev -y
sudo apt install binutils-dev libboost-all-dev autoconf libtool libssl-dev -y
sudo apt install libpixman-1-dev libpython3-dev python3-pip python-capstone virtualenv -y
sudo apt install libncurses-dev flex bison libelf-dev dwarves zstd -y
sudo apt install ninja-build meson libaio-dev -y
sudo apt install libcunit1-dev uuid-dev libaio-dev libssl-dev -y
sudo pip install pyelftools

# inside vm
sudo apt install isal libisal-dev libisal2 -y
sudo apt install nvme-cli

echo "Install SPDK Dependencies ..."

sudo $ULIB_DIR/spdk/scripts/pkgdep.sh --all

