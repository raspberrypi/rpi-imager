#!/bin/bash

######################
## Config variables ##
######################
# required for RNDIS (Windows compatibility)
# Acer Incorporated. - Other hardware - Acer USB Ethernet/RNDIS Gadget 
# did not work correctly
#VID="0x0502"
#PID="0x3230"
# Did work correctly with IBM driver
VID="0x04b3"
PID="0x4010"
SERIAL=$(grep Serial /proc/cpuinfo | awk '{print $3}')
# Compute the SHA256 hash of the serial number
sha=$(echo -n "$SERIAL" | sha256sum | cut -d ' ' -f 1)
config_path=g_ether
##########################
## END Config variables ##
##########################

###############################
## Setup basic configuration ##
###############################
mkdir -p ${config_path}
cd ${config_path} || exit 1

echo "${VID}" > idVendor    # Custom Vendor ID
echo "${PID}" > idProduct   # Custom Product ID
echo 0x0100 > bcdDevice     # v1.0.0
echo 0x0200 > bcdUSB        # USB2
# setup appearance
mkdir -p strings/0x409
echo "${SERIAL}"                > strings/0x409/serialnumber
echo "Raspberry Pi"             > strings/0x409/manufacturer
echo "USB Ethernet Gadget"      > strings/0x409/product
###################################
## END Setup basic configuration ##
###################################

####################################################
## ECM (Ethernet Control Model) for Mac and Linux ##
####################################################
mkdir -p configs/c.1/strings/0x409
mkdir -p functions/ecm.usb0

# max possible stable USB 2.0 power
echo 250 > configs/c.1/MaxPower
echo "ECM" > configs/c.1/strings/0x409/configuration
########################################################
## END ECM (Ethernet Control Model) for Mac and Linux ##
########################################################

#####################################
## RNDIS for Windows compatibility ##
#####################################
mkdir -p configs/c.2/strings/0x409
mkdir -p functions/rndis.usb0
# not allowed
#mkdir -p os_desc/interface.rndis

echo "RNDIS"    > configs/c.2/strings/0x409/configuration
#echo "RNDIS"    > os_desc/interface.rndis/compatibility_id
#echo "5162001"  > os_desc/interface.rndis/sub_compatibility_id
#########################################
## END RNDIS for Windows compatibility ##
#########################################

#########################
## Setup MAC addresses ##
#########################
# Generate MAC addresses using slices of the hash
# TODO: maybe change prefix
mac0=6A:${sha:2:2}:${sha:4:2}:${sha:6:2}:${sha:8:2}:${sha:10:2}
mac1=7A:${sha:2:2}:${sha:4:2}:${sha:6:2}:${sha:8:2}:${sha:10:2}
mac2=8A:${sha:2:2}:${sha:4:2}:${sha:6:2}:${sha:8:2}:${sha:10:2}
mac3=9A:${sha:2:2}:${sha:4:2}:${sha:6:2}:${sha:8:2}:${sha:10:2}

echo "${mac1}" > functions/ecm.usb0/host_addr
echo "${mac3}" > functions/ecm.usb0/dev_addr

echo "${mac0}" > functions/rndis.usb0/host_addr
echo "${mac2}" > functions/rndis.usb0/dev_addr
#############################
## END Setup MAC addresses ##
#############################

######################################
## Link functions to configurations ##
######################################
ln -s functions/ecm.usb0 configs/c.1/
ln -s functions/rndis.usb0 configs/c.2/
##########################################

######################################
## Link the UDC to start the gadget ##
######################################
count=0
while [ ${count} -lt 15 ]; do
   udc="$(ls /sys/class/udc)"
   if [ -n "${udc}" ]; then
      echo "Found UDC ${udc}"
      echo "${udc}" > UDC
      break
   fi
   count=$((count + 1))
   sleep 1
done
##########################################
## END Link the UDC to start the gadget ##
##########################################

systemctl restart getty@ttyGS0

echo "Ethernet USB OTG gadget init complete - $(cat UDC)"
