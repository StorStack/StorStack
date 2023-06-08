#!/usr/bin/env bash

sudo apt install kpartx -y
sudo apt-get install build-essential zlib1g-dev pkg-config libglib2.0-dev -y
sudo apt-get install binutils-dev libboost-all-dev autoconf libtool libssl-dev -y
sudo apt-get install libpixman-1-dev libpython3-dev python3-pip python-capstone virtualenv -y
sudo apt install libncurses-dev flex bison libelf-dev dwarves zstd -y
sudo apt install ninja-build meson libaio-dev -y
sudo apt-get install libcunit1-dev uuid-dev libaio-dev libssl-dev -y
sudo apt install libsdl2-dev -y # Gtk Support
sudo pip install pyelftools
sudo apt install -y libtool automake byacc flex # filebench support
