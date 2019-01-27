#!/bin/bash

export ARCH=arm64

THREAD=-j$(bc <<< $(grep -c ^processor /proc/cpuinfo)+2)

case "$1" in
        15)
            VARIANT="los"
            ;;

        16)
            VARIANT="los16"
            ;;

        *)
            VARIANT="los"
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
