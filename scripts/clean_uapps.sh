#!/bin/bash

source $(dirname "$0")/env.sh

cd $ULIB_DIR
make clean
cd $UAPP_DIR
make clean