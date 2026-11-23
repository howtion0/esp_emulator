# 架构与构建

```text
desktop/src/ui/         界面、黑色设备壳、触摸坐标映射
        ↓ 隔离 IPC
desktop/src/electron/   窗口、系统文件对话框、生命周期
        ↓
desktop/src/core/       QMP、模拟会话、固件校验与合并
        ↓
desktop/src/platform/   可执行文件、ROM、固件路径适配
        ↓ 本机 TCP / 子进程
qemu-source-overlay/   QEMU 的 C 外设模型与修复
```

控制端口只绑定 `127.0.0.1`。渲染进程关闭 Node.js 集成，启用 context isolation 与 sandbox。用户固件导入到应用数据目录，使用 SHA-256 去重；QEMU 通过快照运行，不改写原始固件。

运行库目录按 `runtime/darwin-arm64`、`runtime/linux-x64`、`runtime/win32-x64` 分开。也可通过 `QEMU_BIN` 指定本机二进制；ROM 从对应 `bios/` 或 QEMU 源码树 `pc-bios/` 查找。平台实现不硬编码开发者的用户目录。

## QEMU 修复范围

补齐 GPSPI2/3 与 DMA 数据链路、CO5300 的基本 RGB565 显示、GPIO 触摸中断；修复中断路由、cache 控制寄存器、I2C 事务终止及 SPI Flash 读写边界与 dummy 周期换算。SPIFFS 问题通过修正读 Flash 行为解决，不是跳过固件挂载检查。

这些是针对当前板型和演示固件的功能模型，不包含全部寄存器行为或精确电气时序。

## 构建后端

macOS 使用 `setup/install_macos_homebrew.sh` 准备依赖。Ubuntu/Debian 可参考 `setup/install_ubuntu_deps.sh`。执行：

```sh
JOBS=2 bash scripts/build_qemu.sh
```

脚本优先复用本地原始包，否则克隆 Espressif QEMU 的固定版本 `esp-develop-9.2.2-20260417` 及子模块，应用 overlay 后构建。低内存机器设置 `JOBS=1`。

Windows 在 MSYS2 UCRT64 shell 中安装 GCC、Python、Ninja、pkgconf、glib2、libgcrypt、pixman 及 make、git、diffutils 后运行同一脚本。构建入口处理 Windows `.exe` 名称、绝对安装路径和 UTF-8；解包后只清理带正确 AppleDouble 文件头的 Mac 元数据旁文件。

Windows 运行库可用 `scripts/collect_windows_runtime.py` 收集 MinGW DLL。QEMU 运行还需要 `pc-bios/esp32s3_rev0_rom.bin`。当前未提供安装包，不能仅复制一个 `.exe` 就假定依赖齐全。

Ubuntu 22.04 的 Python 3.10 还需要 `python3-tomli`。Windows 构建已在 MSYS2 UCRT64 上验证；Linux 构建在独立 Ubuntu 22.04 容器完成，运行在 Kali x64。
