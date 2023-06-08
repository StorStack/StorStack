#!/bin/bash

source $(dirname "$0")/env.sh

cd $ULIB_DIR
make -j1
cd $UAPP_DIR
make -j1