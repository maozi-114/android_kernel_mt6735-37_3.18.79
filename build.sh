export ARCH=arm64
export SUBARCH=arm64
export PATH="/home/maozi/tools/gcc/aarch64-linux-android-4.9-gcc/bin:$PATH"
export CC="gcc -B/home/maozi/tools/gcc/aarch64-linux-android-4.9-gcc/libexec/gcc/aarch64-linux-android/4.9.x/"
export CROSS_COMPILE="aarch64-linux-android-"
export TRIPLE="aarch64-linux-android-"
export HOSTCFLAGS="-fcommon"

make O=out yuxin_defconfig
make -j16 O=out 2>&1 | tee build.log

date
