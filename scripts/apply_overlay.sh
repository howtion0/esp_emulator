#!/usr/bin/env bash
# 把 overlay 文件应用到干净 QEMU 源码上
# 用法: bash scripts/apply_overlay.sh /path/to/qemu-source
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OVERLAY="$HERE/../qemu-source-overlay"
QEMU_SRC="${1:?usage: apply_overlay.sh /path/to/qemu-source}"

cp -R "$OVERLAY"/. "$QEMU_SRC"/

# 新版 QEMU 在 --disable-slirp 时仍可能编译 net/slirp.c，这里修掉。
python3 - "$QEMU_SRC/meson.build" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = "if not get_option('slirp').auto() or have_system"
new = "if not get_option('slirp').disabled() and (not get_option('slirp').auto() or have_system)"
if old in s:
    s = s.replace(old, new, 1)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
    print("patched slirp condition")
else:
    print("slirp condition already patched or not found")
PY
printf 'overlay applied to %s\n' "$QEMU_SRC"
