#!/bin/bash

source $(dirname "$0")/env.sh

cd $KERNAL_DIR
sudo make -j$MJOBS
sudo make modules -j$MJOBS