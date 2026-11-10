// A child writes len bytes into a FIFO, then waits on an ack pipe and
// closes the FIFO only once the reader has drained it and parks again, so
// the reader's last wake is the close alone. The reader's end moves to fd
// hi when hi is nonzero, past FD_SETSIZE if hi is.
function fifo_drain(len, hi, k) {
  const ffi = require("bun:ffi");
  const sys = io_sys();
  if (process.platform === "win32") {
    const path = `/tmp/bend_fifo_js_${process.pid}`;
    sys.mkfifo(path);
    let rd = sys.fifo_open(path, 0x800);
    const ack = `\\\\.\\pipe\\bend_ack_${process.pid}_${Date.now()}`;
    const child = Bun.spawn([process.execPath, "-e",
      "const ffi=require('bun:ffi');const k=ffi.dlopen('kernel32.dll',"
      + "{CreateFileA:{args:['ptr','u32','u32','ptr','u32','u32','ptr'],returns:'u64'},"
      + "WriteFile:{args:['u64','ptr','u32','ptr','ptr'],returns:'i32'},"
      + "ReadFile:{args:['u64','ptr','u32','ptr','ptr'],returns:'i32'},"
      + "CloseHandle:{args:['u64'],returns:'i32'},Sleep:{args:['u32'],returns:'void'}});"
      + "const name=Buffer.from(process.argv[1]+'\\0');const n=+process.argv[2];"
      + "const ack=Buffer.from(process.argv[3]+'\\0');let h=-1n;"
      + "for(let i=0;i<400&&(h===-1n||h===0xFFFFFFFFFFFFFFFFn);i++){h=k.symbols.CreateFileA(ffi.ptr(name),0x40000000,0,null,3,0x80,null);if(h===-1n||h===0xFFFFFFFFFFFFFFFFn)k.symbols.Sleep(5);}"
      + "const one=Buffer.from('x');const got=new Uint32Array(1);"
      + "for(let i=0;i<n;i++)k.symbols.WriteFile(h,ffi.ptr(one),1,ffi.ptr(got),null);"
      + "let a=-1n;for(let i=0;i<400&&(a===-1n||a===0xFFFFFFFFFFFFFFFFn);i++){a=k.symbols.CreateFileA(ffi.ptr(ack),0x80000000,0,null,3,0x80,null);if(a===-1n||a===0xFFFFFFFFFFFFFFFFn)k.symbols.Sleep(5);}"
      + "k.symbols.ReadFile(a,ffi.ptr(one),1,ffi.ptr(got),null);k.symbols.CloseHandle(h);",
      "\\\\.\\pipe\\bend" + path.replace(/[\/\\:]/g, "_"), String(len), ack]);
    void child;
    const k32 = ffi.dlopen("kernel32.dll", {
      CreateNamedPipeA: { args: ["ptr", "u32", "u32", "u32", "u32", "u32", "u32", "ptr"], returns: "u64" },
      ConnectNamedPipe: { args: ["u64", "ptr"], returns: "i32" },
      WriteFile: { args: ["u64", "ptr", "u32", "ptr", "ptr"], returns: "i32" },
      CloseHandle: { args: ["u64"], returns: "i32" },
    });
    const ah = k32.symbols.CreateNamedPipeA(Buffer.from(ack + "\0"), 2, 0, 1, 64, 64, 0, null);
    k32.symbols.ConnectNamedPipe(ah, null);
    if (hi !== 0) {
      rd = sys.dup2(rd, hi);
    }
    const b = new Uint8Array(64);
    let total = 0;
    let acked = false;
    const go = () => {
      for (;;) {
        const n = Number(sys.read(rd, sys.ptr(b), 64));
        if (n > 0) {
          total += n;
          continue;
        }
        if (n < 0 && sys.errno() === 11) {
          if (!acked && total === len) {
            const one = Buffer.from("x");
            const got = new Uint32Array(1);
            k32.symbols.WriteFile(ah, ffi.ptr(one), 1, ffi.ptr(got), null);
            k32.symbols.CloseHandle(ah);
            acked = true;
          }
          io_park_on(rd, false, k, go);
          return undefined;
        }
        sys.close(rd);
        return total;
      }
    };
    return go();
  }
  const lib = ffi.dlopen(sys.mac ? "libSystem.dylib" : "libc.so.6", {
    mkfifo: { args: ["cstring", "u32"], returns: "i32" },
    unlink: { args: ["cstring"], returns: "i32" },
    open: { args: ["cstring", "i32"], returns: "i32" },
    dup2: { args: ["i32", "i32"], returns: "i32" },
    pipe: { args: ["ptr"], returns: "i32" },
  }).symbols;
  const path = Buffer.from(`/tmp/bend_fifo_js_${process.pid}\0`);
  const nonblock = sys.mac ? 0x4 : 0x800;
  lib.unlink(path);
  lib.mkfifo(path, 0o600);
  let rd = lib.open(path, nonblock);
  const wr = lib.open(path, 1 | nonblock);
  lib.unlink(path);
  if (hi !== 0) {
    lib.dup2(rd, hi);
    sys.close(rd);
    rd = hi;
  }
  const ack = new Int32Array(2);
  lib.pipe(sys.ptr(ack));
  sys.fcntl(ack[1], 2, 1);
  const sh = `printf %s "${"x".repeat(len)}"; head -c 1 >/dev/null`;
  Bun.spawn(["sh", "-c", sh], { stdin: ack[0], stdout: wr });
  sys.close(ack[0]);
  sys.close(wr);
  const b = new Uint8Array(64);
  const again = sys.mac ? 35 : 11;
  let total = 0;
  let acked = false;
  const go = () => {
    for (;;) {
      const n = Number(sys.read(rd, sys.ptr(b), 64));
      if (n > 0) {
        total += n;
        continue;
      }
      if (n < 0 && sys.errno() === again) {
        if (!acked && total === len) {
          sys.close(ack[1]);
          acked = true;
        }
        io_park_on(rd, false, k, go);
        return undefined;
      }
      sys.close(rd);
      return total;
    }
  };
  return go();
}

io_eff(CID(fifo.drain), fifo_drain);
