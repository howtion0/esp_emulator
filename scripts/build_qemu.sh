#!/usr/bin/env bash
set -euo pipefail
export PYTHONUTF8=1
HERE="$(cd "$(dirname "$0")" && pwd)"
BUNDLE="$(cd "$HERE/.." && pwd)"
QEMU_DIR="$BUNDLE/qemu-source/qemu-esp-develop-9.2.2-20260417-src"
TARBALL="$BUNDLE/qemu-source/esp-develop-9.2.2-20260417.tar.gz"

if [ ! -d "$QEMU_DIR" ]; then
  mkdir -p "$BUNDLE/qemu-source"
  if [ -f "$TARBALL" ]; then
    tar -xf "$TARBALL" -C "$BUNDLE/qemu-source"
  else
    git clone --depth 1 --branch esp-develop-9.2.2-20260417 --recurse-submodules --shallow-submodules https://github.com/espressif/qemu.git "$QEMU_DIR"
  fi
fi

bash "$HERE/apply_overlay.sh" "$QEMU_DIR"
python3 "$HERE/clean_source_metadata.py" "$QEMU_DIR"
mkdir -p "$QEMU_DIR/build"
cd "$QEMU_DIR/build"

EXTRA_CONFIG=()
QEMU_TARGET=qemu-system-xtensa
case "$(uname -s)" in
  MINGW*|MSYS*) QEMU_TARGET=qemu-system-xtensa.exe; EXTRA_CONFIG+=("--prefix=$(cygpath -m "$BUNDLE/runtime/win32-x64")") ;;
esac
PYTHON_BIN="${PYTHON:-$(command -v python3.12 || command -v python3)}"
../configure "${EXTRA_CONFIG[@]}" --python="$PYTHON_BIN" --enable-download \
  --target-list=xtensa-softmmu \
  --enable-gcrypt \
  --disable-docs \
  --disable-capstone \
  --disable-fdt \
  --disable-libdw \
  --disable-blkio \
  --disable-xkbcommon \
  --disable-u2f \
  --disable-smartcard \
  --disable-usb-redir \
  --disable-libusb \
  --disable-gnutls \
  --disable-nettle \
  --disable-canokey \
  --disable-libkeyutils \
  --disable-slirp \
  --disable-dbus-display \
  --disable-sdl \
  --disable-gtk \
  --enable-vnc \
  --disable-jack \
  --disable-sndio \
  --disable-rutabaga-gfx \
  --disable-libpmem \
  --disable-selinux

ninja -j "${JOBS:-2}" "$QEMU_TARGET"
printf '\nQEMU built at:\n%s\n' "$QEMU_DIR/build/$QEMU_TARGET"
