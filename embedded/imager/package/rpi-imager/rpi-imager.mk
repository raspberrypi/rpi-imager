################################################################################
#
# rpi-imager
#
################################################################################

RPI_IMAGER_VERSION = qml
#RPI_IMAGER_SITE = $(call github,raspberrypi,rpi-imager,$(RPI_IMAGER_VERSION))
RPI_IMAGER_SITE = $(TOPDIR)/../../src
RPI_IMAGER_SITE_METHOD = local
RPI_IMAGER_LICENSE = Apache-2.0

RPI_IMAGER_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2 qt5svg qt5tools libarchive libcurl openssl

$(eval $(cmake-package))
