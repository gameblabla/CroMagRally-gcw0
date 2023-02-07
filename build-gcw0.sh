#!/bin/sh
rm -r build
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE="/opt/gcw0-toolchain/usr/share/buildroot/toolchainfile.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-Os -fdata-sections -ffunction-sections -march=mips32r2 -fipa-pta -fno-ipa-cp -ffast-math -fipa-pta" -DCMAKE_EXE_LINKER_FLAGS="-s -Wl,--as-needed -Wl,--gc-sections -flto -Wl,-zcommon-page-size=2097152 -Wl,-zmax-page-size=2097152 -lhugetlbfs" -DCMAKE_INTERPROCEDURAL_OPTIMIZATION="TRUE" ..
make -j8
/opt/gcw0-toolchain/bin/hugeedit --text --data CroMagRally
cd ..
