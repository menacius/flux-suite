#!/usr/bin/env bash
set -Eeuo pipefail

if (($# != 2)); then
  echo "Usage: $0 <cmake-install-prefix> <output-directory>" >&2
  exit 2
fi

INSTALL_PREFIX="$(realpath "$1")"
OUTPUT_DIR="$(mkdir -p "$2" && realpath "$2")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(realpath "$SCRIPT_DIR/../..")"
STAGE_ROOT="$(mktemp -d -t flux-suite-debs.XXXXXXXX)"
trap 'rm -rf -- "$STAGE_ROOT"' EXIT

require_file() {
  [[ -f "$1" ]] || { echo "Required artifact not found: $1" >&2; exit 1; }
}

prepare_package() {
  local package="$1"
  local stage="$STAGE_ROOT/$package"
  install -d "$stage/DEBIAN"
  install -m 0644 "$SCRIPT_DIR/$package/DEBIAN/control" "$stage/DEBIAN/control"
  printf '%s\n' "$stage"
}

build_package() {
  local package="$1"
  local stage="$STAGE_ROOT/$package"
  local version
  version="$(awk '$1 == "Version:" { print $2; exit }' "$stage/DEBIAN/control")"
  [[ -n "$version" ]] || { echo "Missing Version in $package control file" >&2; exit 1; }
  find "$stage" -type d -exec chmod 0755 {} +
  dpkg-deb --build --root-owner-group "$stage" \
    "$OUTPUT_DIR/${package}_${version}_amd64.deb"
}

require_file "$INSTALL_PREFIX/flux-motion-editor/bin/flux-motion"
require_file "$INSTALL_PREFIX/flux-motion-editor/bin/flux-motion-renderer"
require_file "$INSTALL_PREFIX/bin/flux-encoder"
require_file "$INSTALL_PREFIX/bin/flux-encoder-worker"
require_file "$INSTALL_PREFIX/bin/flux-suite"
require_file "$INSTALL_PREFIX/flux-motion/bin/64bit/flux-motion.so"

motion_stage="$(prepare_package flux-motion)"
install -d "$motion_stage/opt/flux-suite/flux-motion" \
  "$motion_stage/usr/bin" "$motion_stage/usr/share/applications" \
  "$motion_stage/usr/share/icons/hicolor/scalable/apps"
cp -a "$INSTALL_PREFIX/flux-motion-editor/bin/." \
  "$motion_stage/opt/flux-suite/flux-motion/"
ln -s /opt/flux-suite/flux-motion/flux-motion "$motion_stage/usr/bin/flux-motion"
install -m 0644 "$SCRIPT_DIR/flux-motion/usr/share/applications/flux-motion.desktop" \
  "$motion_stage/usr/share/applications/flux-motion.desktop"
install -m 0644 "$REPOSITORY_ROOT/apps/flux-suite-installer/resources/icons/flux-motion.svg" \
  "$motion_stage/usr/share/icons/hicolor/scalable/apps/flux-motion.svg"
build_package flux-motion

encoder_stage="$(prepare_package flux-encoder)"
install -d "$encoder_stage/opt/flux-suite/flux-encoder" \
  "$encoder_stage/usr/bin" "$encoder_stage/usr/share/applications" \
  "$encoder_stage/usr/share/icons/hicolor/scalable/apps"
install -m 0755 "$INSTALL_PREFIX/bin/flux-encoder" \
  "$encoder_stage/opt/flux-suite/flux-encoder/flux-encoder"
install -m 0755 "$INSTALL_PREFIX/bin/flux-encoder-worker" \
  "$encoder_stage/opt/flux-suite/flux-encoder/flux-encoder-worker"
cp -a "$INSTALL_PREFIX/bin/providers" \
  "$encoder_stage/opt/flux-suite/flux-encoder/providers"
ln -s /opt/flux-suite/flux-encoder/flux-encoder "$encoder_stage/usr/bin/flux-encoder"
install -m 0644 "$SCRIPT_DIR/flux-encoder/usr/share/applications/flux-encoder.desktop" \
  "$encoder_stage/usr/share/applications/flux-encoder.desktop"
install -m 0644 "$REPOSITORY_ROOT/apps/flux-suite-installer/resources/icons/flux-encoder.svg" \
  "$encoder_stage/usr/share/icons/hicolor/scalable/apps/flux-encoder.svg"
build_package flux-encoder

plugin_stage="$(prepare_package flux-motion-obs-plugin)"
install -d "$plugin_stage/usr/lib/x86_64-linux-gnu/obs-plugins" \
  "$plugin_stage/usr/share/obs/obs-plugins/flux-motion"
install -m 0755 "$INSTALL_PREFIX/flux-motion/bin/64bit/flux-motion.so" \
  "$plugin_stage/usr/lib/x86_64-linux-gnu/obs-plugins/flux-motion.so"
cp -a "$INSTALL_PREFIX/flux-motion/data/." \
  "$plugin_stage/usr/share/obs/obs-plugins/flux-motion/"
build_package flux-motion-obs-plugin

installer_stage="$(prepare_package flux-suite-installer)"
install -d "$installer_stage/usr/bin" "$installer_stage/usr/share/applications" \
  "$installer_stage/usr/share/icons/hicolor/scalable/apps"
install -m 0755 "$INSTALL_PREFIX/bin/flux-suite" "$installer_stage/usr/bin/flux-suite"
install -m 0644 \
  "$SCRIPT_DIR/flux-suite-installer/usr/share/applications/flux-suite.desktop" \
  "$installer_stage/usr/share/applications/flux-suite.desktop"
install -m 0644 "$REPOSITORY_ROOT/apps/flux-suite-installer/resources/icons/flux-suite.svg" \
  "$installer_stage/usr/share/icons/hicolor/scalable/apps/flux-suite.svg"
build_package flux-suite-installer

sha256sum "$OUTPUT_DIR"/*.deb
