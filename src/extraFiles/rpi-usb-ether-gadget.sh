#!/bin/bash

# check if the script is run as root
if [ "$EUID" -ne 0 ]; then
    echo -e "\e[31mPlease run this script as root\e[0m"
    exit
fi

# check if the script is run on a Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/device-tree/model; then
    echo -e "\e[31mThis script is for Raspberry Pi only\e[0m"
    exit
fi

FORCE_ON=false
FORCE_OFF=false
# check if command line arguments to force on or to force off are provided if not continue with
# check else force
if [ "$1" == "on" ]; then
    FORCE_ON=true
elif [ "$1" == "off" ]; then
    FORCE_OFF=true
elif [ "$1" == "toggle" ]; then
    :  # Do nothing and continue to the code below
elif [ "$1" == "status" ]; then
    if [ -f /etc/modules-load.d/usb-ether-gadget.conf ]; then
        echo -e "\e[33mUSB Ethernet Gadget is on\e[0m"
    else
        echo -e "\e[33mUSB Ethernet Gadget is off\e[0m"
    fi
    exit
else # add else if to make toggling possible without toggle keyword
    echo "Usage: rpi-usb-ether-gadget [on|off|toggle|status|help]"
    exit
fi

#rm /etc/modprobe.d/g_ether.conf

# check if /etc/modules-load.d/usb-ether-gadget.conf exists to know if to turn of or on or the force flags are set
if ([ -f /etc/modules-load.d/usb-ether-gadget.conf ] && [ "$FORCE_ON" = false ]) || [ "$FORCE_OFF" = true ]; then
    echo "Turning \e[31moff\e[0m USB Ethernet Gadget mode"
    rm -f /etc/modules-load.d/usb-ether-gadget.conf
    sed -i '/dtoverlay=dwc2,dr_mode=peripheral/d' /boot/firmware/config.txt
else
    echo "Turning \e[32mon\e[0m USB Ethernet Gadget mode"
    echo "dwc2\ng_ether\n" > /etc/modules-load.d/usb-ether-gadget.conf
    echo "\ndtoverlay=dwc2,dr_mode=peripheral\n" >> /boot/firmware/config.txt
fi

echo "Reboot to apply changes"
