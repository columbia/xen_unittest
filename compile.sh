make dist-xen -j10
mkimage -A arm -C none -T kernel -a 0x0200000 -e 0x00200000 -n Xen -d xen/xen uXen
