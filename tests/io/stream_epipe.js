// A child bun gets a pipe whose read end is closed as fd and writes a
// line there with the runtime's own io_out, as IO.print does. The
// answer is the child's status as a shell shows it: 128 plus the signal
// that ended it, else its exit code. JS writes synchronously, so the
// buffered-output case reaches EPIPE at the write, before it could exit.
function epipe_write(fd, resume, k) {
  if (process.platform === "win32") {
    // Bun's spawn only takes Bun descriptors, not the CRT fd that
    // _open_osfhandle mints from a pipe handle (uv_spawn answers EBADF),
    // so the child makes its own pipe, closes the read end and writes:
    // the runtime's own EPIPE answer is exit 141.
    const child = "const ffi=require('bun:ffi');"
      + "const k=ffi.dlopen('kernel32.dll',{CreatePipe:{args:['ptr','ptr','ptr','u32'],returns:'i32'},"
      + "CloseHandle:{args:['u64'],returns:'i32'},WriteFile:{args:['u64','ptr','u32','ptr','ptr'],returns:'i32'},"
      + "GetLastError:{args:[],returns:'u32'}});"
      + "const r=new BigUint64Array(1),w=new BigUint64Array(1);"
      + "if(!k.symbols.CreatePipe(ffi.ptr(r),ffi.ptr(w),null,0))process.exit(1);"
      + "k.symbols.CloseHandle(r[0]);const b=Buffer.from('x\\n');const got=new Uint32Array(1);"
      + "if(!k.symbols.WriteFile(w[0],ffi.ptr(b),2,ffi.ptr(got),null)"
      + "&&[109,232,233].includes(k.symbols.GetLastError()))process.exit(141);"
      + "k.symbols.CloseHandle(w[0]);";
    void fd;
    void resume;
    const r = Bun.spawnSync([process.execPath, "-e", child]);
    return r.exitCode ?? 1;
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
