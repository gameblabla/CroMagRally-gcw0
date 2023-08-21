#!/bin/sh
rm -r build
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE="/opt/gkdminiplus-toolchain/usr/share/buildroot/toolchainfile.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O2 -fdata-sections -ffunction-sections -DREVERSE_AB=1 -flto  -march=armv8.2-a -mtune=cortex-a55" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -fdata-sections -ffunction-sections -DREVERSE_AB=1 -flto -march=armv8.2-a -mtune=cortex-a55" -DCMAKE_EXE_LINKER_FLAGS="-s -Wl,--as-needed -Wl,--gc-sections -flto " -DCMAKE_INTERPROCEDURAL_OPTIMIZATION="TRUE" ..
make -j8
cd ..
