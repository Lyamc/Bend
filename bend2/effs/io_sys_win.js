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
  const k32 = ffi.dlopen("kernel32.dll", {
    FormatMessageA: { args: ["u32", "ptr", "u32", "u32", "ptr", "u32", "ptr"],
      returns: "u32" },
  });
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
    strerror: (code) => {
      const buf = new Uint8Array(256);
      const n = k32.symbols.FormatMessageA(0x1000, null, code, 0, ffi.ptr(buf),
        255, null);
      return n > 0 ? io_text(buf, n).trim() : "error " + code;
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
      return r < 0 ? fail() : r;
    },
    close: (fd) => {
      const r = ws.symbols.closesocket(sok(fd));
      tab[fd] = 0n;
      return r === 0 ? 0 : fail();
    },
    fcntl: (fd, cmd, arg) => {
      if (cmd === 3) {
        return 0;
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
      const N = name === 2 ? 4 : name;
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
