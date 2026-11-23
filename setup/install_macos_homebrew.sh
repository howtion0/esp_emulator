#!/usr/bin/env bash
set -euo pipefail
brew install ninja meson pkg-config glib libgcrypt libgpg-error pixman
printf '\n接下来：\nbash scripts/build_qemu.sh\n'
