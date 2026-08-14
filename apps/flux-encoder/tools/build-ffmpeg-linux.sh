#!/usr/bin/env bash
set -euo pipefail

FFMPEG_REF="${FFMPEG_REF:-n8.0}"
NV_CODEC_HEADERS_REF="${NV_CODEC_HEADERS_REF:-n13.0.19.0}"
WORK_ROOT="${WORK_ROOT:-$PWD/.deps/linux/ffmpeg-build}"
OUTPUT_ROOT="${OUTPUT_ROOT:-$PWD/.deps/linux/ffmpeg-custom}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CLEAN="${CLEAN:-0}"

log() { printf '\n==> %s\n' "$*"; }
have_pkg() { pkg-config --exists "$1" 2>/dev/null; }

if [[ "$CLEAN" == "1" ]]; then
  rm -rf "$WORK_ROOT" "$OUTPUT_ROOT"
fi
mkdir -p "$WORK_ROOT/src" "$WORK_ROOT/build" "$OUTPUT_ROOT"

export PKG_CONFIG_PATH="$OUTPUT_ROOT/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export PATH="$OUTPUT_ROOT/bin:$PATH"

log "Building nv-codec-headers $NV_CODEC_HEADERS_REF"
if [[ ! -d "$WORK_ROOT/src/nv-codec-headers/.git" ]]; then
  git clone https://github.com/FFmpeg/nv-codec-headers.git "$WORK_ROOT/src/nv-codec-headers"
fi
git -C "$WORK_ROOT/src/nv-codec-headers" fetch --tags --force
git -C "$WORK_ROOT/src/nv-codec-headers" checkout --force "$NV_CODEC_HEADERS_REF"
make -C "$WORK_ROOT/src/nv-codec-headers" -j"$JOBS" PREFIX="$OUTPUT_ROOT" install

log "Checking out FFmpeg $FFMPEG_REF"
if [[ ! -d "$WORK_ROOT/src/ffmpeg/.git" ]]; then
  git clone https://github.com/FFmpeg/FFmpeg.git "$WORK_ROOT/src/ffmpeg"
fi
git -C "$WORK_ROOT/src/ffmpeg" fetch --tags --force
git -C "$WORK_ROOT/src/ffmpeg" checkout --force "$FFMPEG_REF"

CONFIGURE=(
  --prefix="$OUTPUT_ROOT"
  --bindir="$OUTPUT_ROOT/bin"
  --libdir="$OUTPUT_ROOT/lib"
  --incdir="$OUTPUT_ROOT/include"
  --pkgconfigdir="$OUTPUT_ROOT/lib/pkgconfig"
  --enable-shared
  --disable-static
  --disable-debug
  --enable-pic
  --enable-gpl
  --enable-version3
  --enable-ffnvcodec
  --enable-cuda
  --enable-cuvid
  --enable-nvenc
  --enable-nvdec
  --enable-vaapi
  --enable-vdpau
  --enable-libdrm
  --extra-cflags="-I$OUTPUT_ROOT/include"
  --extra-ldflags="-L$OUTPUT_ROOT/lib -Wl,-rpath,\$ORIGIN/../lib"
)

# Enable optional system libraries only when their development metadata exists.
have_pkg libvpl && CONFIGURE+=(--enable-libvpl)
have_pkg vulkan && CONFIGURE+=(--enable-vulkan)
have_pkg OpenCL && CONFIGURE+=(--enable-opencl)
have_pkg x264 && CONFIGURE+=(--enable-libx264)
have_pkg x265 && CONFIGURE+=(--enable-libx265)
have_pkg vpx && CONFIGURE+=(--enable-libvpx)
have_pkg aom && CONFIGURE+=(--enable-libaom)
have_pkg dav1d && CONFIGURE+=(--enable-libdav1d)
have_pkg opus && CONFIGURE+=(--enable-libopus)
have_pkg vorbis && CONFIGURE+=(--enable-libvorbis)
have_pkg lame && CONFIGURE+=(--enable-libmp3lame)
have_pkg libass && CONFIGURE+=(--enable-libass)
have_pkg freetype2 && CONFIGURE+=(--enable-libfreetype)
have_pkg libwebp && CONFIGURE+=(--enable-libwebp)
have_pkg libplacebo && CONFIGURE+=(--enable-libplacebo)
have_pkg libzimg && CONFIGURE+=(--enable-libzimg)

# CUDA/NPP filters are optional because they require a local CUDA Toolkit and
# make the resulting binary non-redistributable under some configurations.
if [[ -n "${CUDA_HOME:-}" && -x "${CUDA_HOME}/bin/nvcc" ]]; then
  CONFIGURE+=(--enable-cuda-nvcc --enable-libnpp --enable-nonfree)
  CONFIGURE+=(--extra-cflags="-I$OUTPUT_ROOT/include -I${CUDA_HOME}/include")
  CONFIGURE+=(--extra-ldflags="-L$OUTPUT_ROOT/lib -L${CUDA_HOME}/lib64 -Wl,-rpath,\$ORIGIN/../lib")
fi

log "Configuring FFmpeg"
rm -rf "$WORK_ROOT/build/ffmpeg"
mkdir -p "$WORK_ROOT/build/ffmpeg"
cd "$WORK_ROOT/build/ffmpeg"
"$WORK_ROOT/src/ffmpeg/configure" "${CONFIGURE[@]}"

log "Compiling FFmpeg"
make -j"$JOBS"
make install

FFMPEG="$OUTPUT_ROOT/bin/ffmpeg"
FFPROBE="$OUTPUT_ROOT/bin/ffprobe"
[[ -x "$FFMPEG" && -x "$FFPROBE" ]] || { echo "FFmpeg build did not produce ffmpeg and ffprobe" >&2; exit 1; }

log "Validating hardware capabilities"
ENCODERS="$($FFMPEG -hide_banner -encoders 2>&1)"
HWACCELS="$($FFMPEG -hide_banner -hwaccels 2>&1)"
for encoder in h264_nvenc hevc_nvenc; do
  grep -Eq "(^|[[:space:]])${encoder}([[:space:]]|$)" <<<"$ENCODERS" || { echo "Missing required encoder: $encoder" >&2; exit 1; }
done
grep -Eq '^cuda$' <<<"$HWACCELS" || { echo "Missing required CUDA hardware acceleration backend" >&2; exit 1; }
grep -Eq '^vaapi$' <<<"$HWACCELS" || { echo "Missing required VAAPI hardware acceleration backend" >&2; exit 1; }

mkdir -p "$OUTPUT_ROOT/build-info"
$FFMPEG -hide_banner -buildconf > "$OUTPUT_ROOT/build-info/build-info.txt" 2>&1
$FFMPEG -hide_banner -encoders > "$OUTPUT_ROOT/build-info/encoders.txt" 2>&1
$FFMPEG -hide_banner -decoders > "$OUTPUT_ROOT/build-info/decoders.txt" 2>&1
$FFMPEG -hide_banner -hwaccels > "$OUTPUT_ROOT/build-info/hwaccels.txt" 2>&1
$FFMPEG -hide_banner -filters > "$OUTPUT_ROOT/build-info/filters.txt" 2>&1

printf '\nFFmpeg Linux build completed successfully.\nOutput: %s\n' "$OUTPUT_ROOT"
