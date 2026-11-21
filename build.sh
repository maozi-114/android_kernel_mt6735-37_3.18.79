#!/usr/bin/env bash
set -uo pipefail

KDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$KDIR" || exit 1

DEVICE="${1:-yuxin}"
JOBS="${BUILD_JOBS:-$(nproc)}"

TC="$KDIR/toolchains/gcc4.9-aarch64"
REAL_GCC="$TC/bin/921d9846-a815-11e9-84ea-ff8bce1b90b6"

if [ ! -x "$REAL_GCC" ]; then
	echo "错误: 找不到工具链 $REAL_GCC" >&2
	exit 1
fi

fix_py_wrapper() {
	local w="$1"
	[ -e "$w" ] || return 0
	[ -L "$w" ] && return 0
	head -c 32 "$w" 2>/dev/null | grep -q "usr/bin/python" || return 0
	echo "  [toolchain] replace python2 wrapper: $(basename "$w")"
	rm -f "$w"
	ln -s "$(basename "$REAL_GCC")" "$w"
}

if ! command -v python2 >/dev/null 2>&1 && ! command -v python >/dev/null 2>&1; then
	echo "== no python2 found, patching GCC 4.9 prebuilt wrappers =="
	for f in aarch64-linux-android-gcc aarch64-linux-android-g++ \
	         aarch64-linux-android-c++ aarch64-linux-android-gcc-4.9 \
	         aarch64-linux-androidkernel-gcc; do
		fix_py_wrapper "$TC/bin/$f"
	done
fi

need_pkgs=0
for t in make gcc bc bison flex perl python3 git zip; do
	command -v "$t" >/dev/null 2>&1 || { echo "missing: $t"; need_pkgs=1; }
done
if [ "$need_pkgs" -ne 0 ]; then
	echo "== installing host build dependencies =="
	bash "$KDIR/envsetup.sh" || { echo "dependency install failed" >&2; exit 1; }
fi

if [ ! -f "$KDIR/KernelSU/kernel/Kconfig" ]; then
	echo "== initializing KernelSU submodule =="
	if ! git submodule update --init --recursive KernelSU 2>/dev/null; then
		echo "warning: KernelSU submodule fetch failed" >&2
		echo "         try: git -c url.https://ghfast.top/https://github.com/.insteadOf=https://github.com/ submodule update --init --recursive" >&2
	fi
fi

export ARCH=arm64
export SUBARCH=arm64
export PATH="$TC/bin:$PATH"
export CROSS_COMPILE="aarch64-linux-android-"
export CC="$REAL_GCC -B$TC/libexec/gcc/aarch64-linux-android/4.9.x/"
export HOSTCFLAGS="-fcommon"

echo "=============================================="
echo " config   : ${DEVICE}_defconfig"
echo " toolchain: $TC"
echo " compiler : $($REAL_GCC --version | head -1)"
echo " jobs     : $JOBS"
echo " out      : $KDIR/out"
echo "=============================================="

make O=out "${DEVICE}_defconfig" || { echo "config failed" >&2; exit 1; }
make -j"$JOBS" O=out 2>&1 | tee build.log
rc=${PIPESTATUS[0]}

echo
if [ "$rc" -eq 0 ]; then
	echo "build ok:"
	ls -l out/arch/arm64/boot/Image.gz out/arch/arm64/boot/Image.gz-dtb 2>/dev/null
else
	echo "build failed (exit=$rc):" >&2
	grep -nE "error:|Error [0-9]" build.log | head -20 >&2
fi
date
exit "$rc"
