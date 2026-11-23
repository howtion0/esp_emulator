#!/usr/bin/env bash
set -euo pipefail
sudo apt update
sudo apt install -y \
  build-essential ninja-build python3 python3-pip python3-venv python3-tomli \
  pkg-config git curl unzip \
  libglib2.0-dev libgcrypt20-dev libgpg-error-dev \
  zlib1g-dev libpixman-1-dev \
  libslirp-dev
printf '\n接下来：\nbash scripts/build_qemu.sh\n'
