export SCRIPT_DIR=$(dirname $(readlink -f "$0"))
export BASE_DIR=$SCRIPT_DIR/..
export KERNAL_DIR=$BASE_DIR/"linux-5.15"
export QEMU_DIR=$BASE_DIR/"qemu"
export ULIB_DIR=$BASE_DIR/"ulibss"
export UAPP_DIR=$BASE_DIR/"uapps"
export MISC_DIR=$BASE_DIR/"misc"

export WORK_DIR=$BASE_DIR/"workspace"
mkdir -p $WORK_DIR

export MJOBS=20