# Building Flux Suite on Linux

Linux is a supported desktop target. The reference build uses a current
Ubuntu release, GCC, Ninja, Qt 6, the distribution OBS SDK, and the OpenGL OBS
graphics backend. The same C++ sources and rendering pipeline are used by the
Windows and Linux builds.

## Dependencies

On Ubuntu 26.04 or newer:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-svg-dev qt6-websockets-dev qt6-multimedia-dev libqt6sql6-sqlite \
  libcairo2-dev libpango1.0-dev libobs-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libswresample-dev libgl1-mesa-dev ffmpeg unzip
```

Qt 6.5 or newer is required by Flux Encoder and Flux Suite. A distro with an
older Qt can use an official Qt SDK by adding its prefix to
`CMAKE_PREFIX_PATH`.

## Clean suite build

```bash
cmake --preset suite-linux --fresh
cmake --build --preset suite-linux --parallel
ctest --preset suite-linux
```

The standalone applications are written below `out/build/suite-linux`; Flux
Motion's `flux-motion` editor and renderer are in `flux-motion-editor/bin`. The OBS plugin is
staged under `flux-motion/bin/64bit` with its data under `flux-motion/data`.

Install into a private prefix with:

```bash
cmake --install out/build/suite-linux --prefix "$HOME/.local"
```

## Debian packages

Build component-specific Ubuntu 26.04 packages from an installed suite tree:

```bash
cmake --install out/build/suite-linux --prefix "$PWD/out/install/suite-linux"
bash packaging/linux/build-debs.sh \
  "$PWD/out/install/suite-linux" "$PWD/out/packages/linux-x86_64"
```

The command produces `flux-motion`, `flux-encoder`,
`flux-motion-obs-plugin`, and `flux-suite-installer` packages. Install any
combination with APT so its Qt, OBS, FFmpeg, Cairo, Pango, OpenSSL, and other
runtime dependencies are resolved automatically:

```bash
sudo apt install ./out/packages/linux-x86_64/*.deb
```

For a headless startup smoke test, run Qt applications with
`QT_QPA_PLATFORM=offscreen`. GPU validation must run in a graphical session
with a working OpenGL driver; software-only CI can compile and run unit tests
but does not establish hardware acceleration.

Flux Motion locates both the unversioned `libobs-opengl.so` development link
and ABI-versioned runtime modules such as `libobs-opengl.so.30` through
`OBS_STUDIO_BIN_DIR`, the application directory, the directory of the loaded
`libobs`, standard Ubuntu library and `obs-plugins` directories, Snap/AppImage
roots, or an explicit `--obs-bin-root`. Set `OBS_STUDIO_BIN_DIR` when OBS is
installed in another non-standard prefix.
