#!/usr/bin/env bash

# Builds and publishes the MSYS2/MinGW64 version of RWE.

set -euo pipefail

# No cmake: installing it gets exit code 127 when you call it,
# so rely on the one bundled with the machine image instead.
pacman -Sq --needed --noconfirm \
    autoconf \
    automake \
    libtool \
    make \
    unzip \
    mingw-w64-x86_64-toolchain

pacman -Sq --needed --noconfirm \
    mingw-w64-x86_64-boost \
    mingw-w64-x86_64-SDL2 \
    mingw-w64-x86_64-SDL2_image \
    mingw-w64-x86_64-SDL2_mixer \
    mingw-w64-x86_64-glew \
    mingw-w64-x86_64-smpeg2 \
    mingw-w64-x86_64-zlib \
    mingw-w64-x86_64-libpng \
    mingw-w64-x86_64-readline # need up update manually for SDL2_mixer dep

pushd libs
./build-protobuf.sh
popd

mkdir build
pushd build
cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=$Configuration ..
make -j 2
./rwe_test

make -j 2 package

pushd dist
for i in *; do
    appveyor PushArtifact "$i"
done
popd

popd
