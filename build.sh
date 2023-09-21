#!/bin/sh
set -e -x

if [ "$1" = "mrproper" ]; then
	make mrproper
else
	make ${1}_defconfig savedefconfig
	make CROSS_COMPILE=aarch64-linux-gnu- BINMAN_INDIRS="fip/${1}" -j$(nproc)
fi
