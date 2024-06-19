#!/bin/sh

set -e

BUILDROOT=buildroot-2024.02.3
BUILDROOT_TAR=buildroot-2024.02.3.tar.gz

if [ ! -e "${BUILDROOT}" ]; then
    tar xvf "${BUILDROOT_TAR}"
    cd "${BUILDROOT}" && patch -p1 < "${OLDPWD}/buildroot-mesa3d.patch" && cd -
fi

if [ ! -e "${BUILDROOT}/.config" ]; then
    make -C "${BUILDROOT}" BR2_EXTERNAL="${PWD}/imager" rpi-imager_defconfig
fi

#
# Build everything
#
make -C "${BUILDROOT}" BR2_EXTERNAL="$PWD/imager"

#
# Copy the files we are interested in from buildroot's "output/images" directory
# to our "output" directory in top level directory 
#

# Copy Linux kernel and initramfs
cp "${BUILDROOT}/output/images/rootfs.cpio.zst" "${BUILDROOT}/output/images/Image.gz" output
# Raspberry Pi firmware files
cp "${BUILDROOT}/output/images/rpi-firmware/start4.elf" output
cp "${BUILDROOT}/output/images/rpi-firmware/fixup4.dat" output

# Not used by Pi 4, but need to be present to make usbboot think it is a valid directory
touch output/bootcode.bin

cp ${BUILDROOT}/output/images/bcm2711*.dtb output/
cp ${BUILDROOT}/output/images/bcm2712*.dtb output/

mkdir -p output/overlays

cp ${BUILDROOT}/output/images/dwc2-overlay.dtb output/overlays/dwc2.dtbo
cp ${BUILDROOT}/output/images/bcm2712d0-overlay.dtb output/overlays/bcm2712d0.dtbo
cp ${BUILDROOT}/output/images/vc4-kms-v3d-pi5-overlay.dtb output/overlays/vc4-kms-v3d-pi5.dtbo
cp ${BUILDROOT}/output/images/vc4-kms-v3d-pi4-overlay.dtb output/overlays/vc4-kms-v3d-pi4.dtbo
cp ${BUILDROOT}/output/images/disable-bt-overlay.dtb output/overlays/disable-bt.dtbo
cp ${BUILDROOT}/output/images/disable-wifi-overlay.dtb output/overlays/disable-wifi.dtbo
cp ${BUILDROOT}/output/images/disable-bt-pi5-overlay.dtb output/overlays/disable-bt-pi5.dtbo
cp ${BUILDROOT}/output/images/disable-wifi-pi5-overlay.dtb output/overlays/disable-wifi-pi5.dtbo
cp ${BUILDROOT}/output/images/overlay_map.dtb output/overlays/overlay_map.dtb

echo
echo Build complete. Files are in output folder.
echo
