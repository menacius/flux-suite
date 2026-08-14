#!/usr/bin/env bash
set -Eeuo pipefail

SOURCE_DIR="${1:-$(pwd)}"
CONFIG="${FLUX_ENCODER_BUILD_CONFIG:-RelWithDebInfo}"
BUILD_DIR="${FLUX_ENCODER_WSL_BUILD_DIR:-$SOURCE_DIR/build/wsl-ubuntu-lts}"
INSTALL_DIR="${FLUX_ENCODER_WSL_INSTALL_DIR:-$SOURCE_DIR/dist/wsl-ubuntu-lts}"
QT_VERSION="${FLUX_ENCODER_QT_VERSION:-6.8.3}"
QT_ROOT="${FLUX_ENCODER_QT_ROOT:-$HOME/.local/Qt}"
CLEAN="${FLUX_ENCODER_CLEAN_BUILD:-0}"

log() { printf '\n==> %s\n' "$*"; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
trap 'fail "Command failed at line $LINENO: $BASH_COMMAND"' ERR

if ! grep -qiE 'ubuntu' /etc/os-release; then
  fail "This helper is intended for Ubuntu LTS running under WSL."
fi
if ! grep -qiE '(microsoft|wsl)' /proc/version; then
  printf 'Warning: WSL was not detected; continuing as a normal Ubuntu build.\n' >&2
fi

log "Installing Ubuntu build dependencies"
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential ninja-build cmake git curl ca-certificates pkg-config \
  python3 python3-pip python3-venv patchelf rsync \
  ffmpeg libgl1-mesa-dev libegl1-mesa-dev \
  libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
  libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
  libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libdbus-1-3 \
  libfontconfig1 libfreetype6 libnss3 libx11-xcb1 libxrender1 \
  libxi6 libxext6 libxfixes3 libxrandr2 libxkbcommon0

qt_prefix=""
if command -v qmake6 >/dev/null 2>&1; then
  system_qt_version="$(qmake6 -query QT_VERSION 2>/dev/null || true)"
  if python3 - "$system_qt_version" <<'PY'
import sys
from packaging.version import Version
try:
    raise SystemExit(0 if Version(sys.argv[1]) >= Version("6.5.0") else 1)
except Exception:
    raise SystemExit(1)
PY
  then
    qt_prefix="$(qmake6 -query QT_INSTALL_PREFIX)"
  fi
fi

if [[ -z "$qt_prefix" ]]; then
  log "Installing Qt $QT_VERSION with aqtinstall"
  python3 -m venv "$HOME/.cache/flux-encoder-aqt-venv"
  # shellcheck disable=SC1091
  source "$HOME/.cache/flux-encoder-aqt-venv/bin/activate"
  python -m pip install --upgrade pip packaging aqtinstall
  if [[ ! -d "$QT_ROOT/$QT_VERSION/gcc_64" ]]; then
    python -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
      -O "$QT_ROOT"
  fi
  qt_prefix="$QT_ROOT/$QT_VERSION/gcc_64"
else
  # packaging is only needed by the version check above on some minimal systems.
  python3 -m pip install --user --quiet packaging >/dev/null 2>&1 || true
fi

[[ -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]] || \
  fail "Qt6Config.cmake was not found under $qt_prefix"

if [[ "$CLEAN" == "1" ]]; then
  log "Removing previous WSL build"
  rm -rf "$BUILD_DIR" "$INSTALL_DIR"
fi

mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
log "Configuring Flux Encoder"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DCMAKE_PREFIX_PATH="$qt_prefix" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DFLUX_ENCODER_BUILD_TESTS=ON

log "Building Flux Encoder"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

log "Running tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

log "Installing portable WSL/Linux tree"
cmake --install "$BUILD_DIR"
mkdir -p "$INSTALL_DIR/bin"
cat > "$INSTALL_DIR/run-flux-encoder.sh" <<'LAUNCHER'
#!/usr/bin/env bash
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$ROOT/bin:$PATH"
exec "$ROOT/bin/flux-encoder" "$@"
LAUNCHER
chmod +x "$INSTALL_DIR/run-flux-encoder.sh"

archive="$SOURCE_DIR/dist/Flux_Encoder_WSL_Ubuntu_LTS.tar.gz"
log "Creating $archive"
tar -C "$(dirname "$INSTALL_DIR")" -czf "$archive" "$(basename "$INSTALL_DIR")"

printf '\nBuild complete.\nInstall tree: %s\nArchive: %s\n' "$INSTALL_DIR" "$archive"
