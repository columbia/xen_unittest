#make dist-xen XEN_TARGET_ARCH=arm32 CROSS_COMPILE=arm-linux-gnueabihf-
make dist-xen
sudo cp xen/xen /boot/efi/xen/xen.gz
sync
sudo md5sum xen/xen /boot/efi/xen/xen.gz


