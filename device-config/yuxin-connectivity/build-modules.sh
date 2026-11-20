#!/usr/bin/env bash
set -euo pipefail
export ARCH=arm64 SUBARCH=arm64 HOSTCFLAGS=-fcommon MTK_PLATFORM=mt6735
TC=$PWD/toolchains/gcc4.9-aarch64
export PATH=$TC/bin:$PATH CROSS_COMPILE=aarch64-linux-android-
export CC="$TC/bin/921d9846-a815-11e9-84ea-ff8bce1b90b6 -B$TC/libexec/gcc/aarch64-linux-android/4.9.x/"
ref=$PWD/out/yuxin-connectivity-reference
base=$ref/mt6735m-connectivity-source/connectivity-modules/connectivity
(cd out && scripts/mod/modpost -m -S -o yuxin-connectivity-reference/active-kernel.symvers vmlinux.o)
make -j12 -C out M="$base" KBUILD_EXTRA_SYMBOLS="$ref/active-kernel.symvers" CONFIG_MTK_COMBO=m CONFIG_MTK_COMBO_BT=m CONFIG_MTK_COMBO_WIFI=m CONFIG_MTK_COMBO_GPS=m CONFIG_MTK_GPS=m CONFIG_MTK_GPS_EMI=m CONFIG_MTK_FMRADIO=m 'CONFIG_MTK_COMBO_CHIP="CONSYS_6735"' CONFIG_MTK_FM_CHIP=MT6625_FM CONNECTIVITY_UNIFIED_WMT=1 modules > "$ref/modules-build.log" 2>&1
