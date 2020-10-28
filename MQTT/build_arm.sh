#!/bin/sh
mkdir build.paho
cd build.paho
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain_armgnueabi.cmake ..
make
