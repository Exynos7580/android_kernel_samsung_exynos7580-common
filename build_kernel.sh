#!/bin/bash

export ARCH=arm64

THREAD=-j$(bc <<< $(grep -c ^processor /proc/cpuinfo)+2)

case "$1" in
        8)
            VARIANT="los-16"
            ;;

        4)
            VARIANT="los-16Q"
            ;;

        *)
            VARIANT="los-16"
esac

if [ ! -d $(pwd)/output ];
    then
        mkdir $(pwd)/output;
    fi

make -C $(pwd) O=output ARCH=arm64 ""$VARIANT"_a3xelte_defconfig"

make $THREAD -C $(pwd) O=output ARCH=arm64

cp output/arch/arm64/boot/Image  output/arch/arm64/boot/boot.img-kernel
cp output/arch/arm64/boot/dtb.img  output/arch/arm64/boot/boot.img-dt

exit
