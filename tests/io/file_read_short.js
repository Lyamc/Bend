function short_file(n) {
  if (process.platform === "win32") {
    const sys = io_sys();
    const path = "/tmp/bend_read_short_" + process.pid;
    sys.mkfifo(path);
    const rd = sys.fifo_open(path, 0x800);
    const pipe = "\\\\.\\pipe\\bend" + path.replace(/[\/\\:]/g, "_");
    Bun.spawn([process.execPath, "-e",
      "const ffi=require('bun:ffi');const k=ffi.dlopen('kernel32.dll',"
      + "{CreateFileA:{args:['ptr','u32','u32','ptr','u32','u32','ptr'],returns:'u64'},"
      + "WriteFile:{args:['u64','ptr','u32','ptr','ptr'],returns:'i32'},"
      + "CloseHandle:{args:['u64'],returns:'i32'},Sleep:{args:['u32'],returns:'void'}});"
      + "const name=Buffer.from(process.argv[1]+'\\0');const n=+process.argv[2];"
      + "let h=-1n;for(let i=0;i<400&&(h===-1n||h===0xFFFFFFFFFFFFFFFFn);i++){"
      + "h=k.symbols.CreateFileA(ffi.ptr(name),0x40000000,0,null,3,0x80,null);"
      + "if(h===-1n||h===0xFFFFFFFFFFFFFFFFn)k.symbols.Sleep(5);}"
      + "const one=Buffer.from('x');const got=new Uint32Array(1);"
      + "for(let i=0;i<n;i++){k.symbols.WriteFile(h,ffi.ptr(one),1,ffi.ptr(got),null);k.symbols.Sleep(1);}"
      + "k.symbols.CloseHandle(h);",
      pipe, String(n)]);
    sys.fcntl(rd, 4, 0);
    return rd;
  }
  const fs = require("fs");
  const cp = require("child_process");
  const path = "/tmp/bend_read_short_" + process.pid;
  try { fs.unlinkSync(path); } catch (e) {}
  cp.execFileSync("mkfifo", [path]);
  const rd = fs.openSync(path, fs.constants.O_RDONLY | fs.constants.O_NONBLOCK);
  const wr = fs.openSync(path, fs.constants.O_WRONLY | fs.constants.O_NONBLOCK);
  fs.unlinkSync(path);
  cp.spawn("/bin/sh", ["-c",
    "i=0; while [ $i -lt " + n + " ]; do printf x >&3; i=$((i+1)); sleep 0.001; done"],
    { stdio: ["ignore", "ignore", "ignore", wr] });
  fs.closeSync(wr);
  io_sys().fcntl(rd, 4, 0);
  return rd;
}

io_eff(CID(short.file), short_file);
