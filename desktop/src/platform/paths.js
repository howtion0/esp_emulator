const path = require("node:path");
const fs = require("node:fs");
function runtimePaths({
  projectRoot,
  outputDir,
  platform = process.platform,
  arch = process.arch,
  executable = process.env.QEMU_BIN,
}) {
  const name = `qemu-system-xtensa${platform === "win32" ? ".exe" : ""}`;
  const candidates = [
    executable,
    path.join(projectRoot, "runtime", `${platform}-${arch}`, name),
    path.join(
      projectRoot,
      "qemu-source",
      "qemu-esp-develop-9.2.2-20260417-src",
      "build",
      name,
    ),
  ].filter(Boolean);
  return {
    root: projectRoot,
    output: outputDir,
    executable: candidates.find((p) => fs.existsSync(p)) || candidates[0],
    images: fs.existsSync(path.join(projectRoot,"artifacts","flash_images","flower.bin")) ? path.join(projectRoot,"artifacts","flash_images") : path.join(projectRoot,"demos","flowers"),
    bios: fs.existsSync(path.join(projectRoot,"runtime",`${platform}-${arch}`,"bios")) ? path.join(projectRoot,"runtime",`${platform}-${arch}`,"bios") : path.join(projectRoot,"qemu-source","qemu-esp-develop-9.2.2-20260417-src","pc-bios"),
  };
}
module.exports = { runtimePaths };
