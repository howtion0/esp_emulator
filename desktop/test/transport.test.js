const { test } = require("node:test");
const assert = require("node:assert/strict");
const net = require("node:net");
const path = require("node:path");
const { QMP } = require("../src/core/qmp");
const { runtimePaths } = require("../src/platform/paths");
test("QMP matches fragmented and out-of-order responses, propagates errors and disconnects", async () => {
  const server = net.createServer();
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const accepted = new Promise((r) => server.once("connection", r));
  const client = net.createConnection(server.address().port, "127.0.0.1");
  const peer = await accepted;
  const q = new QMP(client);
  let buf = "",
    requests = [];
  peer.on("data", (b) => {
    buf += b;
    let n;
    while ((n = buf.indexOf("\n")) >= 0) {
      requests.push(JSON.parse(buf.slice(0, n)));
      buf = buf.slice(n + 1);
    }
  });
  try {
    const a = q.call("one"),
      b = q.call("two");
    while (requests.length < 2) await new Promise((r) => setTimeout(r, 5));
    peer.write(
      JSON.stringify({ return: { value: 2 }, id: requests[1].id }) +
        "\n" +
        '{"ret',
    );
    peer.write('urn":{"value":1},"id":' + requests[0].id + "}\n");
    assert.deepEqual(await a, { value: 1 });
    assert.deepEqual(await b, { value: 2 });
    const fail = q.call("invalid");
    const rejected = assert.rejects(fail, /unsupported/);
    while (requests.length < 3) await new Promise((r) => setTimeout(r, 5));
    peer.write(
      JSON.stringify({ error: { desc: "unsupported" }, id: requests[2].id }) +
        "\n",
    );
    await rejected;
    const pending = q.call("waiting");
    const disconnected = assert.rejects(pending, /连接已关闭/);
    peer.destroy();
    await disconnected;
  } finally {
    q.close();
    peer.destroy();
    await new Promise((r) => server.close(r));
  }
});
test("platform adapter chooses target binary name without fixed macOS paths", () => {
  for (const platform of ["darwin", "linux", "win32"]) {
    const p = runtimePaths({
      projectRoot: "/not-installed/qingya",
      outputDir: "/tmp/session",
      platform,
      arch: "x64",
      executable: "",
    });
    assert(
      p.executable.endsWith(
        path.join(
          `${platform}-x64`,
          `qemu-system-xtensa${platform === "win32" ? ".exe" : ""}`,
        ),
      ),
    );
  }
});
