EROFS_VERSION = "1.0"

EXTRA_CFLAGS += -Wall -DEROFS_VERSION=\"$(EROFS_VERSION)\"

obj-$(CONFIG_EROFS_FS)	+= erofs.o

ccflags-y := -I$(src)/include

erofs-y := super.o inode.o data.o namei.o dir.o utils.o
erofs-$(CONFIG_EROFS_FS_XATTR)	+= xattr.o
erofs-$(CONFIG_EROFS_FS_ZIP)	+= unzip_vle.o unzip_vle_lz4.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
