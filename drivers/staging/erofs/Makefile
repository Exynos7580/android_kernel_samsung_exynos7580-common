ifneq ($(KERNELRELEASE),)
EROFS_VERSION = "1.0"

CONFIG_EROFS_FS=m
CONFIG_EROFS_FS_XATTR=y
CONFIG_EROFS_FS_ZIP=y

EXTRA_CFLAGS += -Wall -DEROFS_VERSION=\"$(EROFS_VERSION)\" \
	-DCONFIG_EROFS_FS_XATTR -DCONFIG_EROFS_FS_ZIP \
	-DCONFIG_EROFS_FS_CLUSTER_PAGE_LIMIT=1 \
	-DCONFIG_EROFS_FS_ZIP_CACHE_BIPOLAR

obj-$(CONFIG_EROFS_FS) += erofs.o
ccflags-y := -I$(src)/include
erofs-objs := super.o inode.o data.o namei.o dir.o utils.o
erofs-$(CONFIG_EROFS_FS_XATTR) += xattr.o
erofs-$(CONFIG_EROFS_FS_ZIP) += unzip_vle.o unzip_vle_lz4.o unzip_lz4.o
else

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
endif
