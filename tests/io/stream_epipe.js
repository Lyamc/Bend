// A child bun gets a pipe whose read end is closed as fd and writes a
// line there with the runtime's own io_out, as IO.print does. The
// answer is the child's status as a shell shows it: 128 plus the signal
// that ended it, else its exit code. JS writes synchronously, so the
// buffered-output case reaches EPIPE at the write, before it could exit.
function epipe_write(fd, resume, k) {
  if (process.platform === "win32") {
    const ffi = require("bun:ffi");
    const k = ffi.dlopen("kernel32.dll", {
      CreatePipe: { args: ["ptr", "ptr", "ptr", "u32"], returns: "i32" },
      CloseHandle: { args: ["u64"], returns: "i32" },
    });
    const crt = ffi.dlopen("ucrtbase.dll", {
      _open_osfhandle: { args: ["u64", "i32"], returns: "i32" },
    });
    const rd = new BigUint64Array(1);
    const wr = new BigUint64Array(1);
    const sa = new Uint8Array(24);
    new DataView(sa.buffer).setUint32(0, 24, true);
    sa[16] = 1;
    if (k.symbols.CreatePipe(ffi.ptr(rd), ffi.ptr(wr), ffi.ptr(sa), 0) === 0) {
      throw new Error("epipe pipe");
    }
    k.symbols.CloseHandle(rd[0]);
    const held = crt.symbols._open_osfhandle(wr[0], 0);
    const stdio = ["ignore", "inherit", "inherit"];
    stdio[fd] = held;
    const r = Bun.spawnSync([process.execPath, "-e",
      `${io_out}\nio_out(${fd}, Buffer.from("x\\n"));`], { stdio });
    return r.exitCode ?? 128 + (require("os").constants.signals[r.signalCode] ?? 0);
  }
  const ffi = require("bun:ffi");
  const sys = io_sys();
  const lib = ffi.dlopen(sys.mac ? "libSystem.dylib" : "libc.so.6", {
    pipe: { args: ["ptr"], returns: "i32" },
  }).symbols;
  const p = new Int32Array(2);
  lib.pipe(sys.ptr(p));
  sys.close(p[0]);
  const stdio = ["ignore", "inherit", "inherit"];
  stdio[fd] = p[1];
  const r = Bun.spawnSync([process.execPath, "-e",
    `${io_out}\nio_out(${fd}, Buffer.from("x\\n"));`], { stdio });
  sys.close(p[1]);
  return r.exitCode ?? 128 + require("os").constants.signals[r.signalCode];
}

io_eff(CID(Epipe.write), epipe_write);
