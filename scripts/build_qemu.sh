#!/bin/bash

source $(dirname "$0")/env.sh

mkdir -p $QEMU_DIR/build
cd $QEMU_DIR/build
../configure --target-list=x86_64-softmmu
make -j$MJOBS