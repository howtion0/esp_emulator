const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const os = require("node:os");
const path = require("node:path");
const {
  prepareFirmware,
  createFirmwareStore,
  FLASH_SIZE,
} = require("../src/core/firmware");
function image() {
  const b = Buffer.alloc(0x11000, 0xff);
  b[0] = 0xe9;
  b.writeUInt16LE(9, 12);
  b.writeUInt16LE(0x50aa, 0x8000);
  b[0x8002] = 0;
  b[0x8003] = 0;
  b.writeUInt32LE(0x10000, 0x8004);
  b.writeUInt32LE(0x100000, 0x8008);
  b[0x10000] = 0xe9;
  return b;
}
test("imports full image, pads Flash, preserves source and deduplicates persisted catalog", async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "qingya-firmware-"));
  try {
    const source = path.join(dir, "demo.bin"),
      original = image();
    await fs.writeFile(source, original);
    const store = createFirmwareStore(path.join(dir, "library"));
    const item = await store.importFile(source);
    assert.equal((await fs.stat(store.imagePath(item))).size, FLASH_SIZE);
    assert.deepEqual(await fs.readFile(source), original);
    assert.equal((await store.importFile(source)).id, item.id);
    assert.equal(
      (await createFirmwareStore(path.join(dir, "library")).list()).length,
      1,
    );
    const copied = await fs.readFile(store.imagePath(item));
    assert.equal(copied.at(-1), 255);
  } finally {
    await fs.rm(dir, { recursive: true, force: true });
  }
});
test("ESP-IDF flash_files supports spaced paths and rejects overlapping writes", async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "qingya-idf-"));
  try {
    const full = image(),
      manifest = path.join(dir, "flasher_args.json");
    await fs.writeFile(
      path.join(dir, "boot loader.bin"),
      full.subarray(0, 0x8000),
    );
    await fs.writeFile(
      path.join(dir, "partitions.bin"),
      full.subarray(0x8000, 0x9000),
    );
    await fs.writeFile(
      path.join(dir, "application.bin"),
      full.subarray(0x10000),
    );
    const flash_files = {
      "0x0": "boot loader.bin",
      "0x8000": "partitions.bin",
      "0x10000": "application.bin",
    };
    await fs.writeFile(manifest, JSON.stringify({ flash_files }));
    assert.deepEqual(
      (await prepareFirmware(manifest)).image.subarray(0, full.length),
      full,
    );
    flash_files["0x8001"] = "application.bin";
    await fs.writeFile(manifest, JSON.stringify({ flash_files }));
    await assert.rejects(prepareFirmware(manifest), /重叠/);
  } finally {
    await fs.rm(dir, { recursive: true, force: true });
  }
});
test("rejects app-only BIN, wrong chip and oversized partition", async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "qingya-invalid-")),
    file = path.join(dir, "app.bin");
  try {
    const b = image();
    await fs.writeFile(file, b.subarray(0x10000));
    await assert.rejects(prepareFirmware(file), /完整/);
    b.writeUInt16LE(0, 12);
    await fs.writeFile(file, b);
    await assert.rejects(prepareFirmware(file), /ESP32-S3/);
    b.writeUInt16LE(9, 12);
    b.writeUInt32LE(FLASH_SIZE, 0x8008);
    await fs.writeFile(file, b);
    await assert.rejects(prepareFirmware(file), /分区表/);
  } finally {
    await fs.rm(dir, { recursive: true, force: true });
  }
});
