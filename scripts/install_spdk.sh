#!/bin/bash

source $(dirname "$0")/env.sh

echo "Install DPDK ..."

cd $ULIB_DIR/spdk/dpdk
meson build
ninja -C build -j9
cd build
sudo ninja install
sudo ldconfig

pkg-config --cflags libdpdk
pkg-config --libs libdpdk

echo "Install SPDK ..."

cd $ULIB_DIR/spdk
./configure --with-shared
sudo make -j8
sudo make install