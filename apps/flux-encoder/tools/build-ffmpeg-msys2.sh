#!/usr/bin/env bash
set -euo pipefail

on_error() {
  local exit_code=$?
  local line_no=${BASH_LINENO[0]:-unknown}
  printf '\nERROR: FFmpeg bootstrap failed at line %s (exit code %s).\n' "$line_no" "$exit_code" >&2
  printf 'Command: %s\n' "${BASH_COMMAND:-unknown}" >&2
  exit "$exit_code"
}
trap on_error ERR

FFMPEG_REF="${FFMPEG_REF:-n8.0}"
NV_CODEC_HEADERS_REF="${NV_CODEC_HEADERS_REF:-n13.0.19.0}"
WORK_ROOT="${WORK_ROOT:-/c/flux-encoder-ffmpeg-build}"
OUTPUT_ROOT="${OUTPUT_ROOT:-/c/flux-encoder-ffmpeg-output}"
JOBS="${JOBS:-$(nproc)}"

export PATH="/ucrt64/bin:/usr/bin:$PATH"
export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig:/ucrt64/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

log() { printf '\n==> %s\n' "$*"; }
warn() { printf '\nWARNING: %s\n' "$*" >&2; }

if [[ "${FLUX_ENCODER_SKIP_MSYS_UPDATE:-0}" != "1" ]]; then
  log "Updating the MSYS2 package database"
  pacman -Sy --noconfirm
fi

log "Installing the UCRT64 build toolchain"
pacman -S --needed --noconfirm \
  base-devel git make diffutils patch pkgconf nasm yasm \
  mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-clang

# Optional codec/filter dependencies. A missing package must not prevent the
# core FFmpeg/NVIDIA build; configure flags are enabled only when pkg-config
# confirms that the dependency is actually available.
optional_packages=(
  mingw-w64-ucrt-x86_64-x264
  mingw-w64-ucrt-x86_64-x265
  mingw-w64-ucrt-x86_64-aom
  mingw-w64-ucrt-x86_64-dav1d
  mingw-w64-ucrt-x86_64-libvpx
  mingw-w64-ucrt-x86_64-opus
  mingw-w64-ucrt-x86_64-lame
  mingw-w64-ucrt-x86_64-libvorbis
  mingw-w64-ucrt-x86_64-libass
  mingw-w64-ucrt-x86_64-freetype
  mingw-w64-ucrt-x86_64-fontconfig
  mingw-w64-ucrt-x86_64-libwebp
  mingw-w64-ucrt-x86_64-zimg
  mingw-w64-ucrt-x86_64-onevpl
  mingw-w64-ucrt-x86_64-vulkan-headers
  mingw-w64-ucrt-x86_64-vulkan-loader
  mingw-w64-ucrt-x86_64-opencl-headers
  mingw-w64-ucrt-x86_64-ocl-icd
)
for package in "${optional_packages[@]}"; do
  pacman -S --needed --noconfirm "$package" || warn "Optional package unavailable: $package"
done

mkdir -p "$WORK_ROOT" "$OUTPUT_ROOT"
SRC_ROOT="$WORK_ROOT/src"
PREFIX="$WORK_ROOT/prefix"
mkdir -p "$SRC_ROOT" "$PREFIX"

log "Installing NVIDIA codec headers ($NV_CODEC_HEADERS_REF)"
if [[ ! -d "$SRC_ROOT/nv-codec-headers/.git" ]]; then
  git clone https://github.com/FFmpeg/nv-codec-headers.git "$SRC_ROOT/nv-codec-headers"
fi
git -C "$SRC_ROOT/nv-codec-headers" fetch --tags --force
git -C "$SRC_ROOT/nv-codec-headers" checkout --force "$NV_CODEC_HEADERS_REF"
make -C "$SRC_ROOT/nv-codec-headers" PREFIX="$PREFIX" install

log "Installing AMD AMF public headers"
if [[ ! -d "$SRC_ROOT/AMF/.git" ]]; then
  git clone --depth 1 https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git "$SRC_ROOT/AMF"
else
  git -C "$SRC_ROOT/AMF" fetch --depth 1 origin master
  git -C "$SRC_ROOT/AMF" reset --hard origin/master
fi
rm -rf "$PREFIX/include/AMF"
mkdir -p "$PREFIX/include/AMF"
cp -R "$SRC_ROOT/AMF/amf/public/include/"* "$PREFIX/include/AMF/"

log "Checking out FFmpeg ($FFMPEG_REF)"
if [[ ! -d "$SRC_ROOT/ffmpeg/.git" ]]; then
  git clone https://github.com/FFmpeg/FFmpeg.git "$SRC_ROOT/ffmpeg"
fi
git -C "$SRC_ROOT/ffmpeg" fetch --tags --force
git -C "$SRC_ROOT/ffmpeg" checkout --force "$FFMPEG_REF"

git -C "$SRC_ROOT/ffmpeg" clean -xfd
cd "$SRC_ROOT/ffmpeg"

help_text="$(./configure --help)"
configure_flags=(
  "--prefix=$PREFIX"
  "--bindir=$PREFIX/bin"
  "--arch=x86_64"
  "--target-os=mingw32"
  "--enable-gpl"
  "--enable-version3"
  "--enable-shared"
  "--disable-static"
  "--disable-debug"
  "--enable-runtime-cpudetect"
  "--extra-cflags=-I$PREFIX/include"
  "--extra-ldflags=-L$PREFIX/lib"
  "--extra-libs=-lpthread"
)

add_flag_if_supported() {
  local flag="$1"
  if grep -q -- "${flag%%=*}" <<<"$help_text"; then
    configure_flags+=("$flag")
  fi
}

add_pkg_feature() {
  local pkg="$1"
  local flag="$2"
  if pkg-config --exists "$pkg" && grep -q -- "$flag" <<<"$help_text"; then
    configure_flags+=("$flag")
  fi
}

# NVIDIA encode/decode and CUDA frame interoperability. This path uses the
# redistributable nv-codec-headers and does not require the CUDA Toolkit.
add_flag_if_supported --enable-ffnvcodec
add_flag_if_supported --enable-nvenc
add_flag_if_supported --enable-nvdec
add_flag_if_supported --enable-cuvid

# Native Windows and vendor hardware backends.
add_flag_if_supported --enable-d3d11va
add_flag_if_supported --enable-dxva2
add_flag_if_supported --enable-amf
add_flag_if_supported --enable-mediafoundation

# Optional Intel/Vulkan/OpenCL support when the corresponding SDK packages are
# available in MSYS2.
add_pkg_feature vpl --enable-libvpl
add_pkg_feature vulkan --enable-vulkan
add_pkg_feature OpenCL --enable-opencl

# Software codecs and subtitle/scaling features used by Flux Encoder's built-in
# profiles. Missing optional packages simply leave the associated profiles
# unavailable at runtime.
add_pkg_feature x264 --enable-libx264
add_pkg_feature x265 --enable-libx265
add_pkg_feature aom --enable-libaom
add_pkg_feature dav1d --enable-libdav1d
add_pkg_feature vpx --enable-libvpx
add_pkg_feature opus --enable-libopus
add_pkg_feature lame --enable-libmp3lame
add_pkg_feature vorbis --enable-libvorbis
add_pkg_feature libass --enable-libass
add_pkg_feature freetype2 --enable-libfreetype
add_pkg_feature fontconfig --enable-libfontconfig
add_pkg_feature libwebp --enable-libwebp
add_pkg_feature zimg --enable-libzimg

log "Configuring FFmpeg"
printf '  %q' ./configure "${configure_flags[@]}"
printf '\n'
./configure "${configure_flags[@]}"

log "Building FFmpeg"
make -j"$JOBS"
make install

log "Collecting FFmpeg executables and runtime DLLs"
rm -rf "$OUTPUT_ROOT"
mkdir -p "$OUTPUT_ROOT"
cp "$PREFIX/bin/ffmpeg.exe" "$PREFIX/bin/ffprobe.exe" "$OUTPUT_ROOT/"
find "$PREFIX/bin" -maxdepth 1 -type f -iname '*.dll' -exec cp -f '{}' "$OUTPUT_ROOT/" ';'

copy_runtime_deps() {
  local queue_file="$WORK_ROOT/dependency-queue.txt"
  local seen_file="$WORK_ROOT/dependency-seen.txt"
  : > "$queue_file"
  : > "$seen_file"
  find "$OUTPUT_ROOT" -maxdepth 1 -type f \( -iname '*.exe' -o -iname '*.dll' \) -print > "$queue_file"

  while IFS= read -r binary; do
    [[ -f "$binary" ]] || continue
    grep -Fxq "$binary" "$seen_file" && continue
    printf '%s\n' "$binary" >> "$seen_file"

    while IFS= read -r dependency; do
      [[ -f "$dependency" ]] || continue
      case "$dependency" in
        /c/Windows/*|/c/WINDOWS/*) continue ;;
      esac
      local target="$OUTPUT_ROOT/$(basename "$dependency")"
      if [[ ! -f "$target" ]]; then
        cp -f "$dependency" "$target"
        printf '%s\n' "$target" >> "$queue_file"
      fi
    done < <(ldd "$binary" 2>/dev/null | awk '/=> \/.*\.dll/ {print $3} /^\/[A-Za-z].*\.dll/ {print $1}')
  done < "$queue_file"
}
copy_runtime_deps

"$OUTPUT_ROOT/ffmpeg.exe" -hide_banner -version > "$OUTPUT_ROOT/build-info.txt" 2>&1
"$OUTPUT_ROOT/ffmpeg.exe" -hide_banner -buildconf >> "$OUTPUT_ROOT/build-info.txt" 2>&1
"$OUTPUT_ROOT/ffmpeg.exe" -hide_banner -encoders > "$OUTPUT_ROOT/encoders.txt" 2>&1
"$OUTPUT_ROOT/ffmpeg.exe" -hide_banner -hwaccels > "$OUTPUT_ROOT/hwaccels.txt" 2>&1
"$OUTPUT_ROOT/ffmpeg.exe" -hide_banner -filters > "$OUTPUT_ROOT/filters.txt" 2>&1

if ! grep -q 'h264_nvenc' "$OUTPUT_ROOT/encoders.txt"; then
  echo 'The custom FFmpeg build is missing h264_nvenc.' >&2
  exit 30
fi
if ! grep -q 'hevc_nvenc' "$OUTPUT_ROOT/encoders.txt"; then
  echo 'The custom FFmpeg build is missing hevc_nvenc.' >&2
  exit 31
fi
if ! grep -qE '^cuda$|[[:space:]]cuda$' "$OUTPUT_ROOT/hwaccels.txt"; then
  echo 'The custom FFmpeg build is missing CUDA hardware acceleration.' >&2
  exit 32
fi

log "FFmpeg build completed: $OUTPUT_ROOT"
