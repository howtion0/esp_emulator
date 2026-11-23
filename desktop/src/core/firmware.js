// Firmware ingestion is independent of Electron and never writes to source files.
const fs = require("node:fs/promises");
const path = require("node:path");
const crypto = require("node:crypto");
const FLASH_SIZE = 16 * 1024 * 1024;
function validateFlash(bytes) {
  if (bytes.length > FLASH_SIZE || bytes.length < 0x9020)
    throw Error("需要包含启动器、分区表和应用的完整 Flash 镜像，最大 16 MB。");
  if (bytes[0] !== 0xe9 || bytes.readUInt16LE(0x8000) !== 0x50aa)
    throw Error(
      "这不是当前板型的完整 Flash 镜像。单独的应用 BIN 不能启动，请选择合并镜像或 build/flasher_args.json。",
    );
  // ESP image header chip ID: ESP32-S3 = 9.
  if (bytes.readUInt16LE(12) !== 9)
    throw Error("镜像启动器的芯片类型不是 ESP32-S3。");
  let hasApplication = false;
  for (let p = 0x8000; p + 32 <= Math.min(bytes.length, 0x9000); p += 32) {
    if (bytes.readUInt16LE(p) !== 0x50aa) break;
    const offset = bytes.readUInt32LE(p + 4),
      size = bytes.readUInt32LE(p + 8);
    if (!size || offset + size > FLASH_SIZE)
      throw Error("分区表超出当前 16 MB Flash 容量。");
    if (bytes[p + 2] === 0 && offset < bytes.length && bytes[offset] === 0xe9)
      hasApplication = true;
  }
  if (!hasApplication) throw Error("分区表中没有找到已写入的应用镜像。");
}
async function prepareFirmware(source) {
  let bytes;
  if (path.extname(source).toLowerCase() === ".json") {
    const manifest = JSON.parse(await fs.readFile(source, "utf8"));
    if (!manifest.flash_files || typeof manifest.flash_files !== "object")
      throw Error(
        "JSON 中缺少 ESP-IDF 的 flash_files 映射。请选择 build/flasher_args.json。",
      );
    bytes = Buffer.alloc(FLASH_SIZE, 0xff);
    const ranges = [];
    for (const [address, filename] of Object.entries(manifest.flash_files)) {
      const offset = Number(address);
      if (
        !Number.isSafeInteger(offset) ||
        offset < 0 ||
        typeof filename !== "string"
      )
        throw Error("无效的烧录偏移或文件路径。");
      const file = path.resolve(path.dirname(source), filename);
      const stat = await fs.stat(file);
      if (!stat.isFile() || stat.size === 0 || offset + stat.size > FLASH_SIZE)
        throw Error(`固件片段超出容量或不是有效文件：${filename}`);
      if (ranges.some(([a, b]) => offset < b && offset + stat.size > a))
        throw Error("烧录片段有重叠，请检查 flasher_args.json。");
      ranges.push([offset, offset + stat.size]);
      (await fs.readFile(file)).copy(bytes, offset);
    }
  } else {
    const stat = await fs.stat(source);
    if (!stat.isFile() || stat.size > FLASH_SIZE)
      throw Error("请选择最大 16 MB 的完整 Flash BIN。");
    bytes = await fs.readFile(source);
  }
  validateFlash(bytes);
  const image = Buffer.alloc(FLASH_SIZE, 0xff);
  bytes.copy(image);
  return {
    image,
    sha256: crypto.createHash("sha256").update(image).digest("hex"),
  };
}
function createFirmwareStore(directory) {
  const catalog = path.join(directory, "catalog.json");
  async function list() {
    try {
      const data = JSON.parse(await fs.readFile(catalog, "utf8"));
      return Array.isArray(data) ? data : [];
    } catch (e) {
      if (e.code === "ENOENT") return [];
      throw e;
    }
  }
  async function importFile(source) {
    const { image, sha256 } = await prepareFirmware(source);
    const id = `import-${sha256.slice(0, 16)}`,
      file = `${id}.bin`;
    await fs.mkdir(directory, { recursive: true });
    const items = await list();
    if (items.some((item) => item.id === id))
      return items.find((item) => item.id === id);
    const name =
      path.extname(source).toLowerCase() === ".json"
        ? path.basename(JSON.parse(await fs.readFile(source,"utf8")).app?.file || path.basename(path.dirname(source)),".bin")
        : path.basename(source, ".bin");
    const item = {
      id,
      name,
      file,
      sha256,
      importedAt: new Date().toISOString(),
    };
    await fs.writeFile(path.join(directory, file), image);
    await fs.writeFile(
      catalog + ".tmp",
      JSON.stringify([...items, item], null, 2),
    );
    await fs.rename(catalog + ".tmp", catalog);
    return item;
  }
  return {
    list,
    importFile,
    imagePath: (item) => path.join(directory, item.file),
  };
}
module.exports = {
  FLASH_SIZE,
  validateFlash,
  prepareFirmware,
  createFirmwareStore,
};
