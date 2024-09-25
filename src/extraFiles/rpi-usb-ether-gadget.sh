#!/bin/bash

# check if the script is run as root
if [ "$EUID" -ne 0 ]; then
    echo -e "\e[31mPlease run this script as root\e[0m"
    exit
fi

# check if the script is run on a Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/device-tree/model; then
    echo -e "\e[31mThis script is for Raspberry Pi devices only\e[0m"
    exit
fi

TURN_ON=true
if [ "$1" == "on" ]; then
    TURN_ON=true
elif [ "$1" == "off" ]; then
    TURN_ON=false
elif [ "$1" == "toggle" ]; then
    if [ -f /etc/modules-load.d/usb-ether-gadget.conf ]; then
        TURN_ON=false
    fi
elif [ "$1" == "status" ]; then
    if [ -f /etc/modules-load.d/usb-ether-gadget.conf ]; then
        echo -e "\e[33mUSB Ethernet Gadget is on\e[0m"
    else
        echo -e "\e[33mUSB Ethernet Gadget is off\e[0m"
    fi
    exit
else
    echo "Usage: rpi-usb-ether-gadget [on|off|toggle|status|help]"
    exit
fi

#rm /etc/modprobe.d/g_ether.conf

if [ "$TURN_ON" = false ]; then
    echo "Turning \e[31moff\e[0m USB Ethernet Gadget mode"
    rm -f /etc/modules-load.d/usb-ether-gadget.conf
    sed -i '/dtoverlay=dwc2,dr_mode=peripheral/d' /boot/firmware/config.txt
else
    echo "Turning \e[32mon\e[0m USB Ethernet Gadget mode"
    echo "dwc2\ng_ether\n" > /etc/modules-load.d/usb-ether-gadget.conf
    echo "\ndtoverlay=dwc2,dr_mode=peripheral\n" >> /boot/firmware/config.txt
fi

echo "Reboot to apply changes"
