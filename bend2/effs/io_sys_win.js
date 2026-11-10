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
    + " setsockopt:Uiipu>i getsockopt:Uiipp>i WSAPoll:pui>i"));
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
      const r = ws.symbols.closesocket(sok(fd));
      tab[fd] = 0n;
      return r === 0 ? 0 : fail();
    },
    fcntl: (fd, cmd, arg) => {
      if (cmd === 3) {
        return win_appends(fd) ? 0x400 : 0;
      }
      if (cmd === 4) {
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
        if (ev) {
          fds.push({ fd, ev });
        }
      }
      if (r) {
        r.fill(0);
      }
      if (w) {
        w.fill(0);
      }
      if (fds.length === 0) {
        if (ms > 0 && typeof Bun !== "undefined" && Bun.sleepSync) {
          Bun.sleepSync(ms);
        }
        return 0;
      }
      const raw = new Uint8Array(fds.length * 16);
      const wdv = new DataView(raw.buffer);
      for (let i = 0; i < fds.length; i += 1) {
        wdv.setBigUint64(i * 16, sok(fds[i].fd) ?? 0n, true);
        wdv.setInt16(i * 16 + 8, fds[i].ev, true);
      }
      const got = ws.symbols.WSAPoll(ffi.ptr(raw), fds.length, ms);
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
      return ready;
    },
  };
}
