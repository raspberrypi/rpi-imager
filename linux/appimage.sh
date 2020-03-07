#!/bin/bash

# This script compiles the application and generates an AppImage
# containing the application and a minimal subset of Qt and Qml
# that is required to run it
#
# It is intended to be running on the oldest still-supported version
# of Ubuntu, which currently is xenial

sudo add-apt-repository ppa:beineri/opt-qt-5.14.1-xenial -y
sudo apt-get update -qq
sudo apt-get -y install qt514base qt514declarative qt514quickcontrols qt514quickcontrols2 libgl1-mesa-dev curl libarchive-dev
source /opt/qt*/bin/qt*-env.sh

# Compile and install into AppDir
cmake . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
make DESTDIR=appdir -j$(nproc) install ; find appdir/

# Convert AppDir into AppImage
wget -c -nv "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt-continuous-x86_64.AppImage
./linuxdeployqt-continuous-x86_64.AppImage appdir/usr/share/applications/*.desktop -appimage
