#!/bin/sh
# Cross-build librmpp-refresh.so. SDK defaults to ../sdk (Arm GNU aarch64 + Qt 6.10 headers + device libs).
set -e
cd "$(dirname "$0")"
SDK=${SDK:-../sdk}
CXX=${CXX:-$SDK/bin/aarch64-none-linux-gnu-g++}
Q=$SDK/qt/usr/include/qt6
mkdir -p build
"$CXX" -std=c++17 -O2 -fPIC -shared -DQT_NO_VERSION_TAGGING -DQT_NO_DEBUG \
  -I"$Q" -I"$Q/QtCore" -I"$Q/QtGui" -I"$Q/QtQml" -I"$Q/QtQuick" \
  src/rmpprefresh.cpp -o build/librmpp-refresh.so -L"$SDK/devlibs" \
  -l:libQt6Core.so.6 -l:libQt6Gui.so.6 -l:libQt6Qml.so.6 -l:libQt6Quick.so.6 \
  -Wl,--allow-shlib-undefined -Wl,-soname,librmpp-refresh.so
ls -la build
