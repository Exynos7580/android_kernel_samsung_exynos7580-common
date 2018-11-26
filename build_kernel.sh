#!/bin/bash

export ARCH=arm64

if [ ! -d $(pwd)/output ];
    then
        mkdir $(pwd)/output;
    fi

make -C $(pwd) O=output ARCH=arm64 "los_a3xelte_defconfig"
make -j7 -C $(pwd) O=output ARCH=arm64

cp output/arch/arm64/boot/Image  output/arch/arm64/boot/boot.img-kernel
cp output/arch/arm64/boot/dtb.img  output/arch/arm64/boot/boot.img-dt

exit
