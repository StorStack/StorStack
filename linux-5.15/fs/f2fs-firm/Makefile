# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_F2FS_FIRM_FS) += f2fs-firm.o

f2fs-firm-y		:= dir.o file.o inode.o namei.o hash.o super.o inline.o
f2fs-firm-y		+= checkpoint.o gc.o data.o node.o segment.o recovery.o
f2fs-firm-y		+= shrinker.o extent_cache.o sysfs.o
f2fs-firm-$(CONFIG_F2FS_FIRM_STAT_FS) += debug.o
f2fs-firm-$(CONFIG_F2FS_FIRM_FS_XATTR) += xattr.o
f2fs-firm-$(CONFIG_F2FS_FIRM_FS_POSIX_ACL) += acl.o
f2fs-firm-$(CONFIG_FS_VERITY) += verity.o
f2fs-firm-$(CONFIG_F2FS_FIRM_FS_COMPRESSION) += compress.o
f2fs-firm-$(CONFIG_F2FS_FIRM_IOSTAT) += iostat.o
