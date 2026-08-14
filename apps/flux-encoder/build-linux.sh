#!/usr/bin/env bash
set -euo pipefail

CONFIGURATION="RelWithDebInfo"
BUILD_DIR=""
DEPS_DIR=""
PACKAGE_DIR=""
CLEAN=0
SKIP_TESTS=0
NO_INSTALL=0
USE_SYSTEM_FFMPEG=0
REBUILD_FFMPEG=0

usage() {
  cat <<USAGE
Usage: ./build-linux.sh [options]
  --configuration <Debug|Release|RelWithDebInfo|MinSizeRel>
  --build-dir <path>
  --dependencies-dir <path>
  --package-dir <path>
  --clean
  --skip-tests
  --no-install          Do not install missing system packages
  --use-system-ffmpeg   Do not build the Flux Encoder FFmpeg bundle
  --rebuild-ffmpeg
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration) CONFIGURATION="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --dependencies-dir) DEPS_DIR="$2"; shift 2 ;;
    --package-dir) PACKAGE_DIR="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    --no-install) NO_INSTALL=1; shift ;;
    --use-system-ffmpeg) USE_SYSTEM_FFMPEG=1; shift ;;
    --rebuild-ffmpeg) REBUILD_FFMPEG=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build/linux-release}"
DEPS_DIR="${DEPS_DIR:-$SOURCE_DIR/.deps/linux}"
REPOSITORY_ROOT="$(cd "$SOURCE_DIR/../.." && pwd)"
PACKAGE_DIR="${PACKAGE_DIR:-$REPOSITORY_ROOT/out/dist/linux-x86_64/Flux Encoder}"
FFMPEG_PREFIX="$DEPS_DIR/ffmpeg-custom"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

log() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }
run_root() { if [[ ${EUID:-$(id -u)} -eq 0 ]]; then "$@"; else sudo "$@"; fi; }

install_packages() {
  [[ "$NO_INSTALL" == "1" ]] && return 0
  log "Checking and installing Linux build dependencies"
  if have apt-get; then
    run_root apt-get update
    run_root apt-get install -y \
      build-essential cmake ninja-build git pkg-config nasm yasm python3 \
      qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite \
      libdrm-dev libva-dev libvdpau-dev libvulkan-dev ocl-icd-opencl-dev \
      libx264-dev libx265-dev libvpx-dev libaom-dev libdav1d-dev \
      libopus-dev libvorbis-dev libmp3lame-dev libass-dev libfreetype6-dev \
      libwebp-dev libzimg-dev || true
    # oneVPL package name differs across supported Debian/Ubuntu releases.
    run_root apt-get install -y libvpl-dev 2>/dev/null || true
  elif have dnf; then
    run_root dnf install -y gcc gcc-c++ make cmake ninja-build git pkgconf-pkg-config nasm yasm python3 \
      qt6-qtbase-devel libdrm-devel libva-devel libvdpau-devel vulkan-headers vulkan-loader-devel \
      ocl-icd-devel x264-devel x265-devel libvpx-devel libaom-devel libdav1d-devel \
      opus-devel libvorbis-devel lame-devel libass-devel freetype-devel libwebp-devel zimg-devel || true
    run_root dnf install -y oneVPL-devel 2>/dev/null || true
  elif have pacman; then
    run_root pacman -Syu --needed --noconfirm \
      base-devel cmake ninja git pkgconf nasm yasm python qt6-base \
      libdrm libva libvdpau vulkan-headers vulkan-icd-loader ocl-icd \
      x264 x265 libvpx aom dav1d opus libvorbis lame libass freetype2 libwebp zimg vpl-runtime || true
  elif have zypper; then
    run_root zypper --non-interactive install -y \
      gcc gcc-c++ make cmake ninja git pkg-config nasm yasm python3 qt6-base-devel \
      libdrm-devel libva-devel libvdpau-devel vulkan-devel OpenCL-Headers ocl-icd-devel \
      libx264-devel libx265-devel libvpx-devel libaom-devel libdav1d-devel \
      libopus-devel libvorbis-devel libmp3lame-devel libass-devel freetype2-devel libwebp-devel || true
  else
    echo "Unsupported package manager. Install CMake, Ninja, Qt 6 development files, Git, pkg-config, NASM/YASM and FFmpeg dependencies manually, or use --no-install." >&2
  fi
}

install_packages
for tool in cmake git pkg-config; do have "$tool" || { echo "Required tool not found: $tool" >&2; exit 1; }; done
if ! have ninja; then GENERATOR_ARGS=(); else GENERATOR_ARGS=(-G Ninja); fi

if [[ "$CLEAN" == "1" ]]; then rm -rf "$BUILD_DIR" "$PACKAGE_DIR"; fi
mkdir -p "$DEPS_DIR"

if [[ "$USE_SYSTEM_FFMPEG" == "0" ]]; then
  log "Building the Flux Encoder FFmpeg bundle with NVENC/CUDA, VAAPI and optional QSV/Vulkan/OpenCL"
  CLEAN="$REBUILD_FFMPEG" WORK_ROOT="$DEPS_DIR/ffmpeg-build" OUTPUT_ROOT="$FFMPEG_PREFIX" JOBS="$JOBS" \
    "$SOURCE_DIR/tools/build-ffmpeg-linux.sh"
  FFMPEG_BIN="$FFMPEG_PREFIX/bin"
  export LD_LIBRARY_PATH="$FFMPEG_PREFIX/lib:${LD_LIBRARY_PATH:-}"
else
  FFMPEG_BIN="$(dirname "$(command -v ffmpeg)")"
fi

log "Configuring Flux Encoder"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$CONFIGURATION" \
  -DFLUX_ENCODER_FFMPEG_DIR="$FFMPEG_BIN"

log "Building Flux Encoder"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [[ "$SKIP_TESTS" == "0" ]]; then
  log "Running tests"
  PATH="$FFMPEG_BIN:$PATH" LD_LIBRARY_PATH="$FFMPEG_PREFIX/lib:${LD_LIBRARY_PATH:-}" \
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

log "Packaging portable Linux directory"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR/bin" "$PACKAGE_DIR/lib" "$PACKAGE_DIR/providers"
for executable in flux-encoder flux-encoder-worker; do
  found="$(find "$BUILD_DIR" -type f -name "$executable" -perm -111 | head -n1 || true)"
  [[ -n "$found" ]] || { echo "Built executable not found: $executable" >&2; exit 1; }
  cp -a "$found" "$PACKAGE_DIR/bin/"
done
if [[ "$USE_SYSTEM_FFMPEG" == "0" ]]; then
  cp -a "$FFMPEG_PREFIX/bin/ffmpeg" "$FFMPEG_PREFIX/bin/ffprobe" "$PACKAGE_DIR/bin/"
  cp -a "$FFMPEG_PREFIX/lib/"*.so* "$PACKAGE_DIR/lib/" 2>/dev/null || true
  cp -a "$FFMPEG_PREFIX/build-info" "$PACKAGE_DIR/" 2>/dev/null || true
fi
cp -a "$SOURCE_DIR/providers/." "$PACKAGE_DIR/providers/" 2>/dev/null || true
cp -a "$SOURCE_DIR/VERSION.txt" "$PACKAGE_DIR/"
cat > "$PACKAGE_DIR/flux-encoder.sh" <<'LAUNCHER'
#!/usr/bin/env bash
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$HERE/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/bin/flux-encoder" "$@"
LAUNCHER
chmod +x "$PACKAGE_DIR/flux-encoder.sh"

printf '\nLinux build completed.\nPackage: %s\n' "$PACKAGE_DIR"
