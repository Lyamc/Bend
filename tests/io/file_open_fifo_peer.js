// The peer: echo the first line to the real stdout, write "hi" into the
// FIFO ($1) and remove it, then copy the rest through. A run that dies
// first must not leave it holding stdout: no line, it exits; a FIFO still
// unread after 10 s, a watchdog removes it and kills the peer.
const PEER_SH = `IFS= read -r l || { rm -f "$1"; exit 1; };`
  + ` printf '%s\\n' "$l"; (sleep 10; [ -p "$1" ] && rm -f "$1" && kill $$)`
  + ` >/dev/null 2>&1 & printf hi > "$1"; rm -f "$1"; exec cat`;

let peer_out = -1;

function peer_lib() {
  const sys = io_sys();
  return require("bun:ffi").dlopen(sys.mac ? "libSystem.dylib" : "libc.so.6", {
    mkfifo: { args: ["cstring", "u32"], returns: "i32" },
    dup: { args: ["i32"], returns: "i32" },
    dup2: { args: ["i32", "i32"], returns: "i32" },
    pipe: { args: ["ptr"], returns: "i32" },
  }).symbols;
}

// Moves fd 1 onto a pipe the peer reads, and answers the FIFO's path. The
// pipe's write end is close-on-exec, so the peer does not hold it open.
function peer_arm() {
  if (process.platform === "win32") {
    const sys = io_sys();
    const fs = require("fs");
    const ffi = require("bun:ffi");
    const path = `/tmp/bend_peer_js_${process.pid}`;
    sys.mkfifo(path);
    const pipe = "\\\\.\\pipe\\bend" + path.replace(/[\/\\:]/g, "_");
    const real = fs.writeSync;
    let buf = "";
    let started = false;
    fs.writeSync = (fd, data, ...rest) => {
      if (fd === 1 && !started) {
        const text = Buffer.isBuffer(data) ? data : Buffer.from(data);
        buf += text.toString();
        const n = real.call(fs, fd, data, ...rest);
        if (buf.includes("\n")) {
          started = true;
          fs.writeSync = real;
          Bun.spawn([process.execPath, "-e",
            "const ffi=require('bun:ffi');const k=ffi.dlopen('kernel32.dll',"
            + "{CreateFileA:{args:['ptr','u32','u32','ptr','u32','u32','ptr'],returns:'u64'},"
            + "WriteFile:{args:['u64','ptr','u32','ptr','ptr'],returns:'i32'},"
            + "CloseHandle:{args:['u64'],returns:'i32'},Sleep:{args:['u32'],returns:'void'}});"
            + "const name=Buffer.from(process.argv[1]+'\\0');let h=-1n;"
            + "for(let i=0;i<400&&(h===-1n||h===0xFFFFFFFFFFFFFFFFn);i++){"
            + "h=k.symbols.CreateFileA(ffi.ptr(name),0x40000000,0,null,3,0x80,null);"
            + "if(h===-1n||h===0xFFFFFFFFFFFFFFFFn)k.symbols.Sleep(5);}"
            + "const hi=Buffer.from('hi\\n');const got=new Uint32Array(1);"
            + "k.symbols.WriteFile(h,ffi.ptr(hi),3,ffi.ptr(got),null);k.symbols.CloseHandle(h);",
            pipe]);
        }
        return n;
      }
      return real.call(fs, fd, data, ...rest);
    };
    peer_out = 1;
    void ffi;
    return io_text(Buffer.from(path), path.length);
  }
  const sys = io_sys();
  const lib = peer_lib();
  const path = `/tmp/bend_peer_js_${process.pid}`;
  require("fs").rmSync(path, { force: true });
  const p = new Int32Array(2);
  if (lib.mkfifo(Buffer.from(path + "\0"), 0o600) !== 0
    || lib.pipe(sys.ptr(p)) !== 0) {
    throw "bend: peer_arm: the FIFO or pipe failed";
  }
  sys.fcntl(p[1], 2, 1);
  Bun.spawn(["sh", "-c", PEER_SH, "sh", path],
    { stdin: p[0], stdout: "inherit" });
  sys.close(p[0]);
  peer_out = lib.dup(1);
  lib.dup2(p[1], 1);
  sys.close(p[1]);
  return io_text(Buffer.from(path), path.length);
}

// Puts fd 1 back; the peer's cat sees the pipe close and exits.
function peer_done() {
  if (process.platform === "win32") {
    return { $: CID(Unit) };
  }
  peer_lib().dup2(peer_out, 1);
  io_sys().close(peer_out);
  return { $: CID(Unit) };
}

io_eff(CID(peer_arm), peer_arm);
io_eff(CID(peer_done), peer_done);
