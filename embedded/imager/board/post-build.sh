#!/bin/sh

#
# Trim down images by removing some files we do not need
#

(cd ${TARGET_DIR}/bin

rm -f choom col colcrt colrm column fincore findmnt flock \
  getopt hexdump ipcmk isosize look lscpu lsipc lsirq lslocks \
  lsns mcookie namei pcre2grep pcre2test prlimit qml qmlpreview \
  qmlscene qmltestrunner renice rev script scriptlive scriptreplay \
  setarch setsid uuidgen uuidparse whereis xmlwf zstd zstdgrep zstdless \
  zstdmt zstdcat linux32 linux64 lzmadec lzmainfo uname26 unzstd unxz xz \
  xzcat xzcmp xzdev xzdiff xzegrep xzfgrep xzgrep xzless xzmore)

(cd ${TARGET_DIR}/usr/sbin

rm -f blkdiscard blkid blkzone blockdev chcpu ctrlaltdel fdisk findfs \
  fsfreeze fstrim ldattach mkswap readprofile rtcwake swaplabel \
  swapoff swapon)

rm -rf ${TARGET_DIR}/usr/lib/metatypes ${TARGET_DIR}/usr/lib/qt/plugins/qmltooling 
rm -f ${TARGET_DIR}/usr/lib/qt/plugins/platforms/libqvnc.so ${TARGET_DIR}/usr/lib/libQt5QuickTest.so* ${TARGET_DIR}/usr/lib/libQt5Test.so*
