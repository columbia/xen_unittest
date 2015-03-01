#!/bin/bash
#make dist-xen XEN_TARGET_ARCH=arm32 CROSS_COMPILE=arm-linux-gnueabihf-

if [[ -f .localconf ]]; then
        source .localconf
else
	ARCH="arm64"

        echo -n "What's the architecture? [$ARCH]:"
        read _ARCH
        if [[ -n "$_ARCH" ]]; then
               TARCH="$_ARCH"
        fi
	echo "ARCH=\"$ARCH\"" > .localconf
fi

if [[ "$ARCH" == "arm64" ]]; then
	CROSS_COMPILER=aarch64-linux-gnu-
fi

make dist-xen XEN_TARGET_ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILER

echo "copy to?"
read _COPYTO

if [[ "$_COPYTO" == "y" ]]; then
	md5sum xen/xen
	scp xen/xen s2r:/boot/efi/xen/xen.gz
	ssh s2r sync
	ssh s2r md5sum /boot/efi/xen/xen.gz
fi
