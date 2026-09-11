#!/bin/sh
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Present an emulated USB device to this host, so the real libusb code in
# rpiboot/libusb_transport.cpp can be tested without a Compute Module.
#
# There is no way to reach LibusbContext or LibusbTransport with a fake: they
# exist precisely to talk to libusb, and libusb talks to the kernel. So this
# gives the kernel a device to talk about. Two in-tree modules do the work:
#
#   usbip-vudc  a USB device controller implemented in software. A gadget
#               bound to it behaves like a device plugged into a port that
#               does not physically exist.

set -e

GADGET_NAME=rpiboot_test
CONFIGFS=/sys/kernel/config
G=$CONFIGFS/usb_gadget/$GADGET_NAME
USBIPD_MARKER=/run/rpiboot_test_usbipd_started

# Does this machine have everything the emulation needs? Prints the reason it
# does not, so a skipped test can say why rather than just vanishing.
do_check() {
    [ "$(uname -s)" = "Linux" ] || { echo "not Linux"; return 1; }
    [ "$(id -u)" = "0" ] || { echo "not root"; return 1; }
    command -v usbip >/dev/null 2>&1 || { echo "usbip not installed"; return 1; }
    command -v usbipd >/dev/null 2>&1 || { echo "usbipd not installed"; return 1; }
    for m in libcomposite usbip-vudc vhci-hcd; do
        modprobe "$m" 2>/dev/null || { echo "cannot load $m"; return 1; }
    done
    [ -d $CONFIGFS/usb_gadget ] || mount -t configfs none $CONFIGFS 2>/dev/null || true
    [ -d $CONFIGFS/usb_gadget ] || { echo "no configfs usb_gadget"; return 1; }
    [ -e /sys/class/udc/usbip-vudc.0 ] || { echo "no usbip-vudc UDC"; return 1; }
    echo "ok"
}

# Detach whichever vhci port currently holds an imported device. There is only
# ever one because this script only ever attaches one.
detach_all() {
    usbip port 2>/dev/null | sed -n 's/^Port \([0-9]*\): <Port in Use>.*/\1/p' | while read -r p; do
        usbip detach -p "$p" >/dev/null 2>&1 || true
    done
    wait_for_ports_free || true
}

# `usbip detach` returns once the request is queued; the port stays in use
# until the kernel has finished retiring the device. Attaching the next one
# inside that window either fails outright -- "attach failed" -- or leaves the
# previous device on the bus long enough for a scan to find it, which reads as
# one Compute Module generation being reported as another.
wait_for_ports_free() {
    i=0
    while [ $i -lt 100 ]; do
        usbip port 2>/dev/null | grep -q "<Port in Use>" || return 0
        i=$((i + 1))
        usleep 50000 2>/dev/null || sleep 0.05
    done
    return 1
}

do_down() {
    detach_all
    if [ -d $G ]; then
        echo "" > $G/UDC 2>/dev/null || true
        find $G/configs -maxdepth 2 -type l -exec rm -f {} + 2>/dev/null || true
        rmdir $G/configs/*/strings/* 2>/dev/null || true
        rmdir $G/configs/* 2>/dev/null || true
        rmdir $G/functions/* 2>/dev/null || true
        rmdir $G/strings/* 2>/dev/null || true
        rmdir $G 2>/dev/null || true
    fi
    # Kill by recorded pid, not by pattern: `pkill -f usbipd` also matches any
    # shell whose command line happens to mention it, which includes the one
    # running this script when it is invoked with the name on the line.
    if [ -f $USBIPD_MARKER ]; then
        kill "$(cat $USBIPD_MARKER)" 2>/dev/null || true
        rm -f $USBIPD_MARKER
    fi
    echo "down"
}

do_up() {
    vid=$1
    pid=$2
    ifaces=${3:-1}

    do_down >/dev/null 2>&1 || true

    mkdir -p $G
    echo "$vid" > $G/idVendor
    echo "$pid" > $G/idProduct
    echo 0x0200 > $G/bcdUSB
    mkdir -p $G/strings/0x409
    echo "0123456789abcdef" > $G/strings/0x409/serialnumber
    echo "Raspberry Pi Test" > $G/strings/0x409/manufacturer
    echo "Emulated USB boot device" > $G/strings/0x409/product

    mkdir -p $G/configs/c.1/strings/0x409
    echo "emulated" > $G/configs/c.1/strings/0x409/configuration
    echo 120 > $G/configs/c.1/MaxPower

    # Loopback gives interface 0. A second function is only added when the
    # test wants the multi-interface descriptor layout, because that is what
    # LibusbTransport's endpoint selection keys off.
    mkdir -p $G/functions/Loopback.0
    ln -s $G/functions/Loopback.0 $G/configs/c.1/
    if [ "$ifaces" -ge 2 ]; then
        mkdir -p $G/functions/SourceSink.0
        ln -s $G/functions/SourceSink.0 $G/configs/c.1/
    fi

    echo usbip-vudc.0 > $G/UDC

    # Leave a daemon we did not start alone; something else on this machine
    # may be relying on it.
    if ! pgrep -x usbipd >/dev/null 2>&1; then
        usbipd --device -D
        pgrep -x usbipd > $USBIPD_MARKER 2>/dev/null || true
    fi

    # usbipd needs a moment to open its socket after starting.
    i=0
    while [ $i -lt 50 ]; do
        if usbip attach -r 127.0.0.1 -b usbip-vudc.0 >/dev/null 2>&1; then
            echo "up"
            return 0
        fi
        i=$((i + 1))
        usleep 100000 2>/dev/null || sleep 0.1
    done
    echo "attach failed" >&2
    return 1
}

case "$1" in
    check) do_check ;;
    up)    shift; do_up "$@" ;;
    down)  do_down ;;
    *)     echo "usage: $0 {check|up <vid> <pid> <ifaces>|down}" >&2; exit 2 ;;
esac
