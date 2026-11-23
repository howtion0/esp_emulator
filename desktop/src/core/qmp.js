class QMP {
  constructor(socket) {
    this.socket = socket;
    this.pending = new Map();
    this.id = 0;
    this.buf = "";
    socket.on("data", (b) => {
      this.buf += b.toString();
      let n;
      while ((n = this.buf.indexOf("\n")) >= 0) {
        const line = this.buf.slice(0, n);
        this.buf = this.buf.slice(n + 1);
        try {
          const m = JSON.parse(line);
          if (m.id && this.pending.has(m.id)) {
            const p = this.pending.get(m.id);
            this.pending.delete(m.id);
            clearTimeout(p.timer);
            m.error ? p.reject(Error(m.error.desc)) : p.resolve(m.return);
          }
        } catch {}
      }
    });
    socket.on("error", () => {});
    socket.on("close", () => {
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(Error("模拟器连接已关闭"));
      }
      this.pending.clear();
    });
  }
  call(execute, args = {}) {
    return new Promise((resolve, reject) => {
      const id = ++this.id;
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(Error("模拟器响应超时"));
      }, 5000);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.write(
        JSON.stringify({ execute, arguments: args, id }) + "\n",
      );
    });
  }
  close() {
    this.socket.destroy();
  }
}

module.exports = { QMP };
