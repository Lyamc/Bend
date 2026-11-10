// Spliced into the JS runtime by comp.ts. Not an effect: nothing imports it.
// Winsock behind the same calls the Unix runtime makes on libc. Socket
// handles are table indexes, because select's bitsets are fd-sized.

function io_sys_win(ffi) {
  const T = { i: "i32", u: "u32", U: "u64", I: "i64", p: "ptr" };
  const spec = (s) => Object.fromEntries(s.split(" ").map((x) => {
    const [name, args, ret] = x.split(/[:>]/);
    return [name, { args: [...(args ?? "")].map((a) => T[a] ?? "ptr"),
      returns: T[ret] ?? "i32" }];
  }));
  const ws = ffi.dlopen("ws2_32.dll", spec(
    "WSAStartup:up>i WSAGetLastError:>i socket:iii>U bind:Upu>i listen:Ui>i"
    + " connect:Upu>i accept:Upp>U send:UpUi>i recv:UpUi>i sendto:UpUipu>i"
    + " recvfrom:UpUipp>i closesocket:U>i ioctlsocket:Uup>i"
    + " setsockopt:Uiipu>i getsockopt:Uiipp>i getsockname:Upp>i WSAPoll:pui>i"));
  const wsa = new Uint8Array(512);
  ws.symbols.WSAStartup(0x0202, ffi.ptr(wsa));
  const tab = [];
  const dead = (s) => s === undefined || s === 0n || s === -1n
    || s === 0xFFFFFFFFFFFFFFFFn || Number(s) === -1;
  const put = (s) => {
    const v = typeof s === "bigint" ? s : BigInt(s);
    if (dead(v)) {
      return -1;
    }
    tab.push(v);
    return tab.length - 1;
  };
  const sok = (fd) => tab[fd];
  const last = { n: 0 };
  const map_err = (e) => e === 10035 ? 11 : e === 10036 ? 115 : e;
  let append_ask = null;
  const win_appends = (fd) => {
    try {
      if (append_ask === null) {
        const crt = ffi.dlopen("ucrtbase.dll", {
          _get_osfhandle: { args: ["i32"], returns: "u64" },
        });
        const ntdll = ffi.dlopen("ntdll.dll", {
          NtQueryInformationFile: { args: ["u64", "ptr", "ptr", "u32", "i32"],
            returns: "i32" },
        });
        append_ask = { crt, ntdll };
      }
      const h = append_ask.crt.symbols._get_osfhandle(fd);
      if (h === 0n || h === 0xFFFFFFFFFFFFFFFFn) {
        return false;
      }
      const iosb = new Uint8Array(16);
      const mode = new Uint32Array(1);
      const st = append_ask.ntdll.symbols.NtQueryInformationFile(h,
        ffi.ptr(iosb), ffi.ptr(mode), 4, 16);
      return st === 0 && (mode[0] & 4) !== 0;
    } catch (_) {
      return false;
    }
  };
  const k32 = ffi.dlopen("kernel32.dll", spec(
    "CreateNamedPipeA:puuuuuup>U CreateFileA:puuuuup>U CloseHandle:U>i"
    + " ConnectNamedPipe:Up>i ReadFile:Upuup>i WriteFile:Upuup>i"
    + " PeekNamedPipe:Upupppp>i GetLastError:>u CreateEventA:piip>U"
    + " WaitForSingleObject:Uu>u GetOverlappedResult:Uppi>i Sleep:u>"));
  const crt = ffi.dlopen("ucrtbase.dll", spec(
    "_open_osfhandle:Ui>i _get_osfhandle:i>U _dup2:ii>i _setmaxstdio:i>i"));
  const pipes = new Map();
  const fifos = new Map();
  const pipe_name = (path) => "\\\\.\\pipe\\bend"
    + String(path).replace(/[\/\\:]/g, "_");
  const hdead = (h) => h === 0n || h === -1n || h === 0xFFFFFFFFFFFFFFFFn;
  let intr_at = 0;
  const ov_new = () => {
    const buf = new Uint8Array(32);
    const ev = k32.symbols.CreateEventA(null, 1, 0, null);
    new DataView(buf.buffer).setBigUint64(16, ev, true);
    return { buf, ev };
  };
  const fifo_io = (h, ptr, n, writing) => {
    const ov = ov_new();
    const got = new Uint32Array(1);
    const ok = writing
      ? k32.symbols.WriteFile(h, ptr, n, ffi.ptr(got), null)
      : k32.symbols.ReadFile(h, ptr, n, ffi.ptr(got), ffi.ptr(ov.buf));
    let e = ok ? 0 : k32.symbols.GetLastError();
    if (!ok && e === 997) {
      k32.symbols.WaitForSingleObject(ov.ev, 0xFFFFFFFF);
      if (k32.symbols.GetOverlappedResult(h, ffi.ptr(ov.buf), ffi.ptr(got), 0)) {
        e = 0;
      } else {
        e = k32.symbols.GetLastError();
      }
    }
    k32.symbols.CloseHandle(ov.ev);
    if (e === 0) {
      return got[0];
    }
    if (!writing && (e === 109 || e === 233)) {
      return 0;
    }
    last.n = (writing && (e === 109 || e === 232)) ? 32 : e === 232 ? 11 : 5;
    return -1;
  };
  const fifo_open = (path, flags) => {
    const name = pipes.get(path);
    if (name === undefined) {
      last.n = 2;
      return -1;
    }
    const wr = (flags & 1) !== 0 || (flags & 2) !== 0;
    let h = -1n;
    if (!wr) {
      h = k32.symbols.CreateNamedPipeA(Buffer.from(name + "\0"),
        0x00000001 | 0x40000000, 0, 1, 65536, 65536, 0, null);
      if (hdead(h)) {
        last.n = 5;
        return -1;
      }
      const ov = ov_new();
      const ok = k32.symbols.ConnectNamedPipe(h, ffi.ptr(ov.buf));
      const e = ok ? 0 : k32.symbols.GetLastError();
      if (e === 997 && (flags & 0x800) === 0) {
        k32.symbols.WaitForSingleObject(ov.ev, 0xFFFFFFFF);
        k32.symbols.CloseHandle(ov.ev);
      } else if (e !== 0 && e !== 535 && e !== 997) {
        k32.symbols.CloseHandle(ov.ev);
        k32.symbols.CloseHandle(h);
        last.n = 5;
        return -1;
      }
    } else {
      for (let i = 0; i < 200 && hdead(h); i += 1) {
        h = k32.symbols.CreateFileA(Buffer.from(name + "\0"),
          0x40000000, 0, null, 3, 0x80, null);
        if (hdead(h)) {
          k32.symbols.Sleep(5);
        }
      }
      if (hdead(h)) {
        last.n = 2;
        return -1;
      }
    }
    const fd = crt.symbols._open_osfhandle(h, wr ? 0 : 0x8000);
    if (fd < 0) {
      k32.symbols.CloseHandle(h);
      last.n = 24;
      return -1;
    }
    fifos.set(fd, { h, nb: (flags & 0x800) !== 0, wr });
    return fd;
  };
  globalThis.BEND_FIFO = fifos;
  globalThis.BEND_FIFO_HAS = (path) => pipes.has(String(path));
  globalThis.BEND_FIFO_OPEN = (path, mode) => {
    const flags = mode === "w" || mode === "a" || mode === "x" ? 1 : 0;
    const fd = fifo_open(String(path), flags);
    return fd < 0 ? -1 : fd;
  };
  globalThis.BEND_FIFO_READ = (fd, buf) => {
    const f = fifos.get(fd);
    if (!f) {
      return -1;
    }
    if (f.nb) {
      const avail = new Uint32Array(1);
      if (!k32.symbols.PeekNamedPipe(f.h, null, 0, null, ffi.ptr(avail), null)) {
        const e = k32.symbols.GetLastError();
        if (e === 109 || e === 233) {
          return 0;
        }
        last.n = 5;
        return -1;
      }
      if (avail[0] === 0) {
        last.n = 11;
        return -1;
      }
      if (avail[0] < buf.length) {
        buf = buf.subarray(0, avail[0]);
      }
    }
    return fifo_io(f.h, ffi.ptr(buf), buf.length, false);
  };
  const fail = () => {
    last.n = map_err(ws.symbols.WSAGetLastError());
    return -1;
  };
  const bytes = (p, n) => (ffi.toArrayBuffer ?? globalThis.Bun?.FFI?.toArrayBuffer)(p, 0, n);
  const view = (p, n) => new Int32Array(bytes(p, n));
  return {
    mac: false,
    ptr: ffi.ptr,
    errno: () => last.n,
    posix: (code) => {
      let n = code | 0;
      if (n < 0) n = -n;
      const uv = { 4093: 7, 4092: 13, 4091: 98, 4090: 99, 4089: 97, 4088: 11,
        4084: 114, 4083: 9, 4082: 16, 4081: 125, 4079: 103, 4078: 111, 4077: 104,
        4076: 89, 4075: 17, 4074: 14, 4073: 113, 4072: 4, 4071: 22, 4070: 5,
        4069: 106, 4068: 21, 4067: 40, 4066: 24, 4065: 90, 4064: 36, 4063: 100,
        4062: 101, 4061: 23, 4060: 105, 4059: 19, 4058: 2, 4057: 12, 4055: 28,
        4054: 38, 4053: 107, 4052: 20, 4051: 39, 4050: 88, 4049: 95, 4048: 1,
        4047: 32, 4046: 71, 4045: 93, 4044: 91, 4043: 30, 4042: 108, 4041: 29,
        4040: 3, 4039: 110, 4038: 26, 4037: 18, 4036: 27, 4035: 92, 4034: 34,
        4033: 6, 4032: 31, 4031: 112, 4030: 121, 4029: 25, 4027: 84, 4026: 75,
        4025: 94 };
      const wsa = { 10013: 13, 10014: 14, 10022: 22, 10024: 24, 10035: 11,
        10036: 115, 10037: 114, 10038: 88, 10039: 89, 10040: 90, 10041: 91,
        10042: 92, 10043: 93, 10044: 94, 10045: 95, 10047: 97, 10048: 98,
        10049: 99, 10050: 100, 10051: 101, 10053: 103, 10054: 104, 10055: 105,
        10056: 106, 10057: 107, 10058: 108, 10060: 110, 10061: 111,
        10064: 112, 10065: 113 };
      return uv[n] ?? wsa[n] ?? n;
    },
    strerror: (code) => {
      const text = { 1: "Operation not permitted", 2: "No such file or directory",
        4: "Interrupted system call", 5: "Input/output error",
        11: "Resource temporarily unavailable", 13: "Permission denied",
        17: "File exists", 20: "Not a directory", 21: "Is a directory",
        9: "Bad file descriptor", 22: "Invalid argument",
        27: "File too large", 28: "No space left on device",
        90: "Message too long",
        32: "Broken pipe", 98: "Address already in use",
        110: "Connection timed out", 111: "Connection refused" };
      const n = (code | 0) < 0 ? -(code | 0) : code | 0;
      return text[n] ?? ("error " + n);
    },
    socket: (d, t, p) => {
      const fd = put(ws.symbols.socket(d, t, p));
      return fd < 0 ? fail() : fd;
    },
    bind: (fd, a, n) => ws.symbols.bind(sok(fd), a, n) === 0 ? 0 : fail(),
    listen: (fd, n) => ws.symbols.listen(sok(fd), n) === 0 ? 0 : fail(),
    connect: (fd, a, n) => {
      if (ws.symbols.connect(sok(fd), a, n) === 0) {
        return 0;
      }
      const e = ws.symbols.WSAGetLastError();
      last.n = e === 10035 ? 115 : map_err(e);
      return -1;
    },
    accept: (fd, a, n) => {
      const got = put(ws.symbols.accept(sok(fd), a, n));
      if (got < 0) {
        last.n = map_err(ws.symbols.WSAGetLastError());
        return -1;
      }
      return got;
    },
    send: (fd, b, n, f) => {
      const r = ws.symbols.send(sok(fd), b, n, f);
      return r < 0 ? fail() : r;
    },
    recv: (fd, b, n, f) => {
      const r = ws.symbols.recv(sok(fd), b, n, f);
      return r < 0 ? fail() : r;
    },
    sendto: (fd, b, n, f, a, al) => {
      const r = ws.symbols.sendto(sok(fd), b, n, f, a, al);
      return r < 0 ? fail() : r;
    },
    recvfrom: (fd, b, n, f, a, al) => {
      const r = ws.symbols.recvfrom(sok(fd), b, n, f, a, al);
      // Winsock drops the tail and reports WSAEMSGSIZE. Linux returns the
      // bytes that fit and keeps the next datagram whole.
      if (r < 0 && ws.symbols.WSAGetLastError() === 10040) {
        return n;
      }
      return r < 0 ? fail() : r;
    },
    close: (fd) => {
      if (fifos.has(fd)) {
        fifos.delete(fd);
        try {
          require("fs").closeSync(fd);
        } catch (e) {
        }
        return 0;
      }
      const r = ws.symbols.closesocket(sok(fd));
      tab[fd] = 0n;
      return r === 0 ? 0 : fail();
    },
    mkfifo: (path) => {
      const key = String(path).replace(/\0+$/, "");
      if (pipes.has(key)) {
        last.n = 17;
        return -1;
      }
      pipes.set(key, pipe_name(key));
      return 0;
    },
    fifo_open: fifo_open,
    read: (fd, ptr, n) => {
      const f = fifos.get(fd);
      if (f) {
        if (f.nb) {
          const avail = new Uint32Array(1);
          if (!k32.symbols.PeekNamedPipe(f.h, null, 0, null, ffi.ptr(avail), null)) {
            const e = k32.symbols.GetLastError();
            if (e === 109 || e === 233) {
              return 0;
            }
            last.n = 5;
            return -1;
          }
          if (avail[0] === 0) {
            last.n = 11;
            return -1;
          }
          if (avail[0] < n) {
            n = avail[0];
          }
        }
        return fifo_io(f.h, ptr, n, false);
      }
      const r = ws.symbols.recv(sok(fd), ptr, n, 0);
      return r < 0 ? fail() : r;
    },
    write: (fd, ptr, n) => {
      const f = fifos.get(fd);
      if (f) {
        return fifo_io(f.h, ptr, n, true);
      }
      const r = ws.symbols.send(sok(fd), ptr, n, 0);
      return r < 0 ? fail() : r;
    },
    dup2: (from, to) => {
      if (to > 512) {
        crt.symbols._setmaxstdio(to + 1 > 8192 ? 8192 : to + 1);
      }
      if (crt.symbols._dup2(from, to) < 0) {
        return -1;
      }
      if (fifos.has(from)) {
        const f = fifos.get(from);
        fifos.set(to, { h: crt.symbols._get_osfhandle(to), nb: f.nb, wr: f.wr });
        fifos.delete(from);
      }
      return to;
    },
    intr: (ms) => {
      intr_at = Date.now() + ms;
    },
    socketpair: (out) => {
      const li = ws.symbols.socket(2, 1, 6);
      const addr = new Uint8Array(16);
      addr[0] = 2;
      addr[4] = 127;
      addr[7] = 1;
      const alen = new Int32Array([16]);
      if (hdead(li) || ws.symbols.bind(li, ffi.ptr(addr), 16) !== 0
        || ws.symbols.listen(li, 1) !== 0) {
        return fail();
      }
      if (ws.symbols.getsockname(li, ffi.ptr(addr), ffi.ptr(alen)) !== 0) {
        return fail();
      }
      const c = ws.symbols.socket(2, 1, 6);
      if (ws.symbols.connect(c, ffi.ptr(addr), 16) !== 0) {
        return fail();
      }
      const s = ws.symbols.accept(li, null, null);
      ws.symbols.closesocket(li);
      out[0] = put(s);
      out[1] = put(c);
      return out[0] < 0 || out[1] < 0 ? -1 : 0;
    },
    getsockname: (fd, a, n) => ws.symbols.getsockname(sok(fd), a, n) === 0
      ? 0 : fail(),
    fcntl: (fd, cmd, arg) => {
      if (cmd === 3) {
        const f = fifos.get(fd);
        if (f) {
          return f.nb ? 0x800 : 0;
        }
        return win_appends(fd) ? 0x400 : 0;
      }
      if (cmd === 4) {
        const f = fifos.get(fd);
        if (f) {
          f.nb = (arg & 0x800) !== 0;
          return 0;
        }
        const nb = new Uint32Array([(arg & 0x800) ? 1 : 0]);
        return ws.symbols.ioctlsocket(sok(fd), 0x8004667E, ffi.ptr(nb)) === 0
          ? 0 : fail();
      }
      last.n = 22;
      return -1;
    },
    setsockopt: (fd, level, name, v, n) => {
      const L = level === 1 ? 0xFFFF : level;
      const opt = { 2: 4, 7: 0x1001, 8: 0x1002 };
      const N = opt[name] ?? name;
      return ws.symbols.setsockopt(sok(fd), L, N, v, n) === 0 ? 0 : fail();
    },
    getsockopt: (fd, level, name, v, n) => {
      const L = level === 1 ? 0xFFFF : level;
      const N = name === 4 ? 0x1007 : name;
      if (ws.symbols.getsockopt(sok(fd), L, N, v, n) !== 0) {
        return fail();
      }
      if (name === 4) {
        const i = view(v, 4);
        i[0] = map_err(i[0]);
      }
      return 0;
    },
    select: (nfds, rptr, wptr, eptr, tvptr) => {
      const nbytes = (nfds + 7) >> 3;
      const u8 = (p) => p == null || p === 0 ? null : new Uint8Array(bytes(p, nbytes));
      const r = u8(rptr);
      const w = u8(wptr);
      let ms = -1;
      if (tvptr) {
        const tv = new BigInt64Array(bytes(tvptr, 16));
        ms = Number(tv[0]) * 1000 + Number(tv[1]) / 1000;
      }
      const fds = [];
      const pipefds = [];
      for (let fd = 0; fd < nfds; fd += 1) {
        const bit = 1 << (fd & 7);
        const at = fd >> 3;
        let ev = 0;
        if (r && (r[at] & bit)) {
          ev |= 0x0300;
        }
        if (w && (w[at] & bit)) {
          ev |= 0x0010;
        }
        if (!ev) {
          continue;
        }
        if (fifos.has(fd)) {
          pipefds.push(fd);
        } else {
          fds.push({ fd, ev });
        }
      }
      if (r) {
        r.fill(0);
      }
      if (w) {
        w.fill(0);
      }
      if (fds.length === 0 && pipefds.length === 0 && intr_at === 0) {
        if (ms > 0 && typeof Bun !== "undefined" && Bun.sleepSync) {
          Bun.sleepSync(ms);
        }
        return 0;
      }
      const started = Date.now();
      const raw = new Uint8Array(Math.max(fds.length, 1) * 16);
      const wdv = new DataView(raw.buffer);
      for (let i = 0; i < fds.length; i += 1) {
        wdv.setBigUint64(i * 16, sok(fds[i].fd) ?? 0n, true);
        wdv.setInt16(i * 16 + 8, fds[i].ev, true);
      }
      const limited = pipefds.length > 0 || intr_at !== 0;
      for (;;) {
        let slice = ms;
        if (intr_at !== 0) {
          const left = Math.max(0, intr_at - Date.now());
          slice = ms < 0 ? left : Math.min(ms, left);
        } else if (limited) {
          slice = ms < 0 ? 10 : Math.min(ms, 10);
        }
        const got = fds.length === 0 ? 0
          : ws.symbols.WSAPoll(ffi.ptr(raw), fds.length, slice | 0);
        if (intr_at !== 0 && Date.now() >= intr_at) {
          intr_at = 0;
          if (r) r.fill(0);
          if (w) w.fill(0);
          last.n = 4;
          return -1;
        }
        if (got < 0) {
          return fail();
        }
        let ready = 0;
        for (let i = 0; i < fds.length; i += 1) {
          const rev = wdv.getInt16(i * 16 + 10, true);
          const fd = fds[i].fd;
          const bit = 1 << (fd & 7);
          const at = fd >> 3;
          if (r && (rev & (0x0300 | 0x0001 | 0x0002))) {
            r[at] |= bit;
            ready += 1;
          }
          if (w && (rev & 0x0010)) {
            w[at] |= bit;
            ready += 1;
          }
        }
        for (const fd of pipefds) {
          const f = fifos.get(fd);
          const avail = new Uint32Array(1);
          let up = false;
          if (!f) {
            up = false;
          } else if (!k32.symbols.PeekNamedPipe(f.h, null, 0, null,
            ffi.ptr(avail), null)) {
            const e = k32.symbols.GetLastError();
            up = e === 109 || e === 233;
          } else {
            up = avail[0] > 0;
          }
          if (up && r) {
            r[fd >> 3] |= 1 << (fd & 7);
            ready += 1;
          }
        }
        if (ready > 0 || !limited) {
          return ready;
        }
        if (ms === 0) {
          return 0;
        }
        if (ms > 0 && Date.now() - started >= ms) {
          return 0;
        }
      }
    },
  };
}
