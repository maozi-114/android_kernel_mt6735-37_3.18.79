#!/usr/bin/env bash
set -euo pipefail

KERNEL_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN="$KERNEL_DIR/toolchains/gcc4.9-aarch64"
TOOLCHAIN_CC="$TOOLCHAIN/bin/aarch64-linux-android-gcc-4.9.x"
usage() {
	cat <<'EOF'
Usage: ./build.sh [pa03|yuxin] [--dry-run]

  pa03     Build pa03_defconfig into out (default).
  yuxin    Build yuxin_defconfig into out-yuxin.
  --dry-run  Show the selected configuration and outputs without building.
  -h, --help Show this help.

Examples:
  ./build.sh
  ./build.sh yuxin
  BUILD_JOBS=8 ./build.sh yuxin
EOF
}

TARGET=""
DRY_RUN=0
for arg in "$@"; do
	case "$arg" in
		-h|--help) usage; exit 0 ;;
		--dry-run) DRY_RUN=1 ;;
		pa03|yuxin)
			if [[ -n "$TARGET" ]]; then
				echo "Select only one build target." >&2
				exit 2
			fi
			TARGET="$arg"
			;;
		*) echo "Unknown argument: $arg" >&2; usage >&2; exit 2 ;;
	esac
done
TARGET="${TARGET:-pa03}"
DEFCONFIG="${TARGET}_defconfig"
case "$TARGET" in
	pa03) OUT_DIR="$KERNEL_DIR/out"; PACKAGE_PREFIX="PA03" ;;
	yuxin) OUT_DIR="$KERNEL_DIR/out-yuxin"; PACKAGE_PREFIX="YUXIN" ;;
esac
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
	echo "BUILD_JOBS must be a positive integer." >&2
	exit 2
fi
if [[ ! -f "$KERNEL_DIR/arch/arm64/configs/$DEFCONFIG" ]]; then
	echo "Missing defconfig: $DEFCONFIG" >&2
	exit 1
fi
printf 'Target: %s\nDefconfig: %s\nOutput: %s\nPackage prefix: %s-BakaSU\nJobs: %s\n' \
	"$TARGET" "$DEFCONFIG" "$OUT_DIR" "$PACKAGE_PREFIX" "$BUILD_JOBS"
if ((DRY_RUN)); then
	exit 0
fi

if [[ "$(uname -m)" != x86_64 ]]; then
	echo "The bundled Android GCC 4.9 toolchain requires an x86_64 Linux host." >&2
	exit 1
fi

if [[ ! -x "$TOOLCHAIN_CC" ]]; then
	echo "Missing AArch64 GCC 4.9 toolchain: $TOOLCHAIN" >&2
	echo "The toolchain should be present in this checkout; see BUILDING.md." >&2
	exit 1
fi

required_host_tools=(make gcc bc perl python3 git zip)
missing_host_tools=()
for tool in "${required_host_tools[@]}"; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		missing_host_tools+=("$tool")
	fi
done
if ((${#missing_host_tools[@]})); then
	printf 'Installing missing host tools: %s\n' "${missing_host_tools[*]}"
	bash "$KERNEL_DIR/envsetup.sh"
	missing_host_tools=()
	for tool in "${required_host_tools[@]}"; do
		if ! command -v "$tool" >/dev/null 2>&1; then
			missing_host_tools+=("$tool")
		fi
	done
	if ((${#missing_host_tools[@]})); then
		printf 'Host setup did not provide required tools: %s\n' "${missing_host_tools[*]}" >&2
		exit 1
	fi
fi

if [[ ! -f "$KERNEL_DIR/KernelSU/kernel/Kconfig" ]]; then
	echo "Initializing BakaSU submodule..."
	git -C "$KERNEL_DIR" submodule update --init --recursive KernelSU
fi

if [[ ! -f "$KERNEL_DIR/KernelSU/kernel/Kconfig" ]]; then
	echo "BakaSU submodule is unavailable; check the KernelSU submodule URL and network access." >&2
	exit 1
fi

export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE="$TOOLCHAIN/bin/aarch64-linux-android-"
export HOSTCFLAGS="${HOSTCFLAGS:--fcommon}"
export PATH="$TOOLCHAIN/bin:$PATH"

make -C "$KERNEL_DIR" O="$OUT_DIR" CROSS_COMPILE="$CROSS_COMPILE" CC="$TOOLCHAIN_CC" "$DEFCONFIG"
make -C "$KERNEL_DIR" O="$OUT_DIR" CROSS_COMPILE="$CROSS_COMPILE" CC="$TOOLCHAIN_CC" -j"$BUILD_JOBS" 2>&1 | tee "$OUT_DIR/build.log"

IMAGE="$OUT_DIR/arch/arm64/boot/Image.gz-dtb"
if [[ ! -s "$IMAGE" ]]; then
	echo "Kernel build succeeded but the expected image is missing: $IMAGE" >&2
	exit 1
fi

PACKAGE_STAGE="$(mktemp -d "$OUT_DIR/.ak3.XXXXXX")"
trap 'rm -rf -- "$PACKAGE_STAGE"' EXIT
cp -a "$KERNEL_DIR/AnyKernel3/." "$PACKAGE_STAGE/"
cp "$IMAGE" "$PACKAGE_STAGE/Image.gz-dtb"
PACKAGE_NAME="${PACKAGE_PREFIX}-BakaSU-$(date +%Y%m%d-%H%M%S).zip"
(
	cd "$PACKAGE_STAGE"
	zip -q -r9 "$OUT_DIR/$PACKAGE_NAME" * -x README.md '*placeholder'
)
echo "AnyKernel3 package: $OUT_DIR/$PACKAGE_NAME"
