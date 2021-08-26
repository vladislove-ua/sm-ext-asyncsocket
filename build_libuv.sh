#!/bin/bash

if [[ -d libuv ]]; then
    exit 0;
fi;

git clone https://github.com/libuv/libuv.git --recursive
cd libuv
sh autogen.sh
./configure --build=i686-pc-linux-gnu "CFLAGS=-m32" "CXXFLAGS=-m32" "LDFLAGS=-m32" --disable-shared --enable-static
make -j 8
cp -R include/ ../../extension/libuv/
cp .libs/libuv.a ../../extension/libuv/
