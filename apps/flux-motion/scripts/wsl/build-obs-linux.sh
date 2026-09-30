#!/usr/bin/env bash
set -Eeuo pipefail

SOURCE=""
WORKSPACE=""
OUTPUT_DIR=""
BUILD_TYPE="RelWithDebInfo"
PACKAGE_NAME="Flux_Motion_linux-x86_64"
ARCHIVE_FORMAT="tar.gz"
INSTALL_DIR=""
BUILD_TESTS=0
INSTALL_DEPS=1
CLEAN=0

while (($#)); do
  case "$1" in
    --source) SOURCE="$2"; shift 2 ;;
    --workspace) WORKSPACE="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --build-type) BUILD_TYPE="$2"; shift 2 ;;
    --package-name) PACKAGE_NAME="$2"; shift 2 ;;
    --archive-format) ARCHIVE_FORMAT="$2"; shift 2 ;;
    --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    --build-tests) BUILD_TESTS=1; shift ;;
    --skip-deps) INSTALL_DEPS=0; shift ;;
    --clean) CLEAN=1; shift ;;
    # The wrapper validates these compatibility constraints before invoking us.
    --expected-version|--max-glibc|--platform) shift 2 ;;
    *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done

[[ -n "$SOURCE" && -n "$WORKSPACE" && -n "$OUTPUT_DIR" ]] || {
  echo "--source, --workspace and --output-dir are required" >&2
  exit 2
}

if ((INSTALL_DEPS)); then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y build-essential cmake ninja-build pkg-config git ca-certificates zip \
    qt6-base-dev qt6-svg-dev qt6-websockets-dev qt6-multimedia-dev \
    libcairo2-dev libpango1.0-dev libobs-dev liblz4-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libswresample-dev libgl1-mesa-dev
fi

BUILD_DIR="$WORKSPACE/build"
STAGE_DIR="$WORKSPACE/stage"
if ((CLEAN)); then
  rm -rf -- "$BUILD_DIR" "$STAGE_DIR"
fi

cmake -S "$SOURCE" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DOBS_FXM_BUILD_TESTS="$([[ "$BUILD_TESTS" == 1 ]] && echo ON || echo OFF)"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"
if ((BUILD_TESTS)); then
  QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
cmake --install "$BUILD_DIR" --prefix "$STAGE_DIR"

if [[ -n "$INSTALL_DIR" ]]; then
  mkdir -p "$INSTALL_DIR"
  cp -a "$STAGE_DIR/flux-motion/." "$INSTALL_DIR/"
fi

mkdir -p "$OUTPUT_DIR"
case "$ARCHIVE_FORMAT" in
  zip)
    (cd "$STAGE_DIR" && zip -qr "$OUTPUT_DIR/$PACKAGE_NAME.zip" .)
    ;;
  tar.gz|tgz)
    tar -C "$STAGE_DIR" -czf "$OUTPUT_DIR/$PACKAGE_NAME.tar.gz" .
    ;;
  both)
    (cd "$STAGE_DIR" && zip -qr "$OUTPUT_DIR/$PACKAGE_NAME.zip" .)
    tar -C "$STAGE_DIR" -czf "$OUTPUT_DIR/$PACKAGE_NAME.tar.gz" .
    ;;
  *)
    printf 'Unsupported archive format: %s\n' "$ARCHIVE_FORMAT" >&2
    exit 2
    ;;
esac

printf 'Flux Motion Linux package written to %s\n' "$OUTPUT_DIR"
