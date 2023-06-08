#!/bin/bash

source $(dirname "$0")/env.sh

$QEMU_DIR/scripts/qmp/qmp-shell $QEMU_DIR/scripts/qmp/qmp-sock
