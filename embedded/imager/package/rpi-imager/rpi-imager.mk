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

# Patches are automatically applied after download and extract, but when using
# a local site there is no download-extract so the patches don't get applied.
# Add a hook to do it manually when using the local site.
define RPI_IMAGER_APPLY_PATCHES
	$(APPLY_PATCHES) $(@D) $(RPI_IMAGER_PKGDIR) *.patch
endef
RPI_IMAGER_POST_RSYNC_HOOKS += RPI_IMAGER_APPLY_PATCHES

$(eval $(cmake-package))
