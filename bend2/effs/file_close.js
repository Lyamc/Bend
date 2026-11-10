// File
// ====

function file_close(file) {
  if (globalThis.BEND_ZERO?.has(file)) {
    globalThis.BEND_ZERO.delete(file);
    return { $: CID(Unit) };
  }
  if (globalThis.BEND_FIFO?.has(file)) {
    globalThis.BEND_FIFO.delete(file);
  }
  const fs = require("fs");
  try {
    fs.closeSync(file);
  } catch (e) {
  }
  io_file_append.delete(file);
  return { $: CID(Unit) };
}

io_eff(CID(File.close), file_close);
