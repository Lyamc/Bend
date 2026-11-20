// File
// ====

// name is the path's bytes, or null if a value is past 255. The special
// names below are ASCII, so decode once to compare them; the open itself
// still takes the exact bytes.
function file_open_with(name, mode) {
  if (name === null) {
    return io_fail(22);
  }
  const shown = typeof name === "string" ? name
    : Buffer.from(name).toString("utf8");
  if (name.includes(0)) {
    return io_fail(process.platform === "darwin" ? 92 : 84);
  }
  if (!["r", "r+", "w", "a", "x"].includes(mode)) {
    return io_fail(22);
  }
  if (process.platform === "win32" && shown === "/etc/hosts") {
    name = (process.env.SystemRoot || "C:\\Windows") + "\\System32\\drivers\\etc\\hosts";
  }
  if (process.platform === "win32" && shown === "/dev/null") {
    if (mode === "x") {
      return io_fail(17);
    }
    name = "NUL";
  }
  if (process.platform === "win32" && shown === "/dev/zero") {
    const held = globalThis.BEND_ZERO ?? (globalThis.BEND_ZERO = new Set());
    const fd = -(held.size + 2);
    held.add(fd);
    return io_done(fd);
  }
  // The standard streams are already open, and there is no portable dup in
  // node:fs; return the process's own descriptor and keep File.close from
  // closing it (see file_close.js).
  if (process.platform === "win32") {
    const std = { "/dev/stdin": 0, "/dev/stdout": 1, "/dev/stderr": 2 };
    const at = std[shown];
    if (at !== undefined) {
      const held = globalThis.BEND_STD ?? (globalThis.BEND_STD = new Set());
      held.add(at);
      return io_done(at);
    }
  }
  if (process.platform === "win32" && globalThis.BEND_FIFO_HAS?.(name)) {
    io_sys();
    const fd = globalThis.BEND_FIFO_OPEN(name, mode);
    if (fd < 0) {
      return io_fail(io_sys().errno() || 5);
    }
    if (mode === "a") {
      io_file_append.add(fd);
    }
    return io_done(fd);
  }
  try {
    // Node spells "x" as "wx".
    const fd = require("fs").openSync(name.length > 0 ? Buffer.from(name) : "",
      mode === "x" ? "wx" : mode, 0o644);
    if (mode === "a") {
      io_file_append.add(fd);
    }
    return io_done(fd);
  } catch (e) {
    return io_fail(-e.errno);
  }
}

function file_open(path, mode, k) {
  io_park_on(undefined, 0, k, () => file_open_with(io_bytes(path), mode), 0);
}

function file_open_bytes(path, mode, k) {
  io_park_on(undefined, 0, k, () => file_open_with(io_unlist(path), mode), 0);
}

io_eff(CID(File.open), file_open);
io_eff(CID(File.open_bytes), file_open_bytes);
