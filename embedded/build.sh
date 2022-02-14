#!/bin/sh

set -e

BUILDROOT=buildroot
BUILDROOT_TAR=buildroot-20220122.tar.bz2

if [ ! -e $BUILDROOT ]; then
    tar xjf $BUILDROOT_TAR
fi

if [ ! -e $BUILDROOT/.config ]; then
    make -C $BUILDROOT BR2_EXTERNAL="$PWD/imager" rpi-imager_defconfig
fi

#
# Build everything
#
make -C $BUILDROOT BR2_EXTERNAL="$PWD/imager"

#
# Copy the files we are interested in from buildroot's "output/images" directory
# to our "output" directory in top level directory 
#

# Copy Linux kernel and initramfs
cp $BUILDROOT/output/images/rootfs.cpio.zst $BUILDROOT/output/images/zImage output
# Raspberry Pi firmware files
cp $BUILDROOT/output/images/rpi-firmware/start4.elf output
cp $BUILDROOT/output/images/rpi-firmware/fixup4.dat output
cp $BUILDROOT/output/images/*.dtb output

# Not used by Pi 4, but need to be present to make usbboot think it is a valid directory
touch output/bootcode.bin

mkdir -p output/overlays

mv -f output/vc4-fkms-v3d-pi4-overlay.dtb output/overlays/vc4-fkms-v3d-pi4.dtbo
mv -f output/vc4-kms-v3d-pi4-overlay.dtb output/overlays/vc4-kms-v3d-pi4.dtbo
mv -f output/disable-bt-overlay.dtb output/overlays/disable-bt.dtbo
mv -f output/disable-wifi-overlay.dtb output/overlays/disable-wifi.dtbo

echo
echo Build complete. Files are in output folder.
echo
