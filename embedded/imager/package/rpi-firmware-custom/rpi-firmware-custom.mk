################################################################################
#
# rpi-firmware
#
################################################################################

RPI_FIRMWARE_CUSTOM_VERSION = 7e9c2063fc15274fbc4dab1ca614f9228834d881
RPI_FIRMWARE_CUSTOM_SITE = $(call github,raspberrypi,firmware,$(RPI_FIRMWARE_CUSTOM_VERSION))
RPI_FIRMWARE_CUSTOM_LICENSE = BSD-3c
RPI_FIRMWARE_CUSTOM_LICENSE_FILES = boot/LICENCE.broadcom
RPI_FIRMWARE_CUSTOM_INSTALL_IMAGES = YES

define RPI_FIRMWARE_CUSTOM_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 $(@D)/boot/bootcode.bin $(BINARIES_DIR)/rpi-firmware/bootcode.bin
	$(INSTALL) -D -m 0644 $(@D)/boot/start.elf $(BINARIES_DIR)/rpi-firmware/start.elf
	$(INSTALL) -D -m 0644 $(@D)/boot/fixup.dat $(BINARIES_DIR)/rpi-firmware/fixup.dat
	$(INSTALL) -D -m 0644 $(@D)/boot/start4.elf $(BINARIES_DIR)/rpi-firmware/start4.elf
	$(INSTALL) -D -m 0644 $(@D)/boot/fixup4.dat $(BINARIES_DIR)/rpi-firmware/fixup4.dat
endef

$(eval $(generic-package))
