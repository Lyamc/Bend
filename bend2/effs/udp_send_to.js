// The IPv6 half of the address codec. comp.ts keeps the IPv4 io_addr, so
// io_addr_ex wraps both into the {buf,len,family} shape the effects use.
// Effect sources are inlined once per path, and a program may use several.
if (globalThis.io_addr_ex === undefined) {

  globalThis.io_addr6 = function (host, port) {
    const mac = io_sys().mac;
    const word = (p) => {
      if (p.includes(".")) {
        const q = p.split(".");
        if (q.length !== 4 || !q.every((x) => /^(0|[1-9]\d{0,2})$/.test(x)
          && Number(x) < 256)) {
          return null;
        }
        return [(Number(q[0]) << 8) | Number(q[1]),
          (Number(q[2]) << 8) | Number(q[3])];
      }
      return /^[0-9a-fA-F]{1,4}$/.test(p) ? [parseInt(p, 16)] : null;
    };
    const parse = (part) => {
      if (part === "") return [];
      const out = [];
      for (const p of part.split(":")) {
        const w = word(p);
        if (w === null) return null;
        out.push(...w);
      }
      return out;
    };
    const h = host.split("::");
    if (h.length > 2) return null;
    const left = parse(h[0]), right = h.length === 2 ? parse(h[1]) : [];
    if (left === null || right === null) return null;
    const missing = 8 - left.length - right.length;
    if (missing < 0 || (h.length === 2 && missing < 1)) return null;
    const words = [...left, ...Array(h.length === 2 ? missing : 0).fill(0),
      ...right];
    if (words.length !== 8) return null;
    const family = mac ? 30 : process.platform === "win32" ? 23 : 10;
    const b = new Uint8Array(28);
    b.set(mac ? [28, family] : [family, 0]);
    b[2] = port >> 8; b[3] = port & 255;
    words.forEach((w, i) => {
      b[8 + 2 * i] = w >> 8;
      b[9 + 2 * i] = w & 255;
    });
    return { buf: b, len: 28, family };
  };

  // inet_ntop's spelling: lower hex, longest zero run as "::" (first on a
  // tie), IPv4-mapped as a dotted quad. Matches the C lane's peer host.
  globalThis.io_addr6_text = function (b) {
    const w = [];
    for (let i = 0; i < 8; i += 1) {
      w.push((b[8 + 2 * i] << 8) | b[9 + 2 * i]);
    }
    if (w.slice(0, 5).every((x) => x === 0) && w[5] === 0xffff) {
      return "::ffff:" + (w[6] >> 8) + "." + (w[6] & 255) + "."
        + (w[7] >> 8) + "." + (w[7] & 255);
    }
    let at = -1, best = 0, s = -1, r = 0;
    for (let i = 0; i <= 8; i += 1) {
      if (i < 8 && w[i] === 0) { if (s < 0) s = i; r += 1; }
      else { if (r > best) { best = r; at = s; } s = -1; r = 0; }
    }
    const h = (x) => x.toString(16);
    return best < 2 ? w.map(h).join(":")
      : w.slice(0, at).map(h).join(":") + "::"
        + w.slice(at + best).map(h).join(":");
  };

  globalThis.io_addr_ex = function (host, port) {
    const v4 = io_addr(host, port);
    if (v4 !== null) {
      return { buf: v4, len: 16, family: 2 };
    }
    return globalThis.io_addr6(host, port);
  };
}

// UDP
// ===

// A datagram goes whole or not at all; a full send buffer (non-blocking,
// so EAGAIN) parks the computation until the socket is writable. b is the
// datagram's bytes, data the value a failed or late send hands back.
function udp_send_to_with(socket, host, port, data, b, k, at) {
  const fail = (code) => io_tup(socket, io_ready(at, io_fail(code, data)));
  const to = io_addr_ex(host, Number(port));
  if (to === null) {
    return fail(22);
  }
  const sys = io_sys();
  const fd = socket;
  const again = sys.mac ? 35 : 11;
  const intr = 4;
  const go = () => {
    while (true) {
      const sent = sys.sendto(fd, b.length ? sys.ptr(b) : null, b.length, 0, sys.ptr(to.buf), to.len);
      if (Number(sent) >= 0) {
        return io_tup(socket, io_ready(at, io_done({ $: CID(Unit) })));
      }
      const code = sys.errno();
      if (code === intr) {
        continue;
      }
      if (code !== again) {
        return fail(code);
      }
      if (io_late(at)) {
        return io_tup(socket, { $: CID(Wait), rest: data });
      }
      io_park_on(fd, true, k, go, at);
      return undefined;
    }
  };
  return go();
}

// A value past 255 fails with EINVAL before the datagram is sent, list kept.
function udp_send_bytes_to_with(socket, host, port, data, k, at) {
  const b = io_unlist(data);
  if (b !== null) {
    return udp_send_to_with(socket, host, port, data, b, k, at);
  }
  return io_tup(socket, io_ready(at, io_fail(22, data)));
}

function udp_send_to(socket, host, port, data, k) {
  return udp_send_to_with(socket, host, port, data, io_bytes(data), k);
}

function udp_send_bytes_to(socket, host, port, data, k) {
  return udp_send_bytes_to_with(socket, host, port, data, k);
}

// The try_ twins pass a deadline at: past it, a send that would wait is
// Wait{data}.
function udp_try_send_to(socket, host, port, data, ms, k) {
  return udp_send_to_with(socket, host, port, data, io_bytes(data), k,
    io_until(ms));
}

function udp_try_send_bytes_to(socket, host, port, data, ms, k) {
  return udp_send_bytes_to_with(socket, host, port, data, k, io_until(ms));
}

io_eff(CID(UDP.send_to), udp_send_to);
io_eff(CID(UDP.send_bytes_to), udp_send_bytes_to);
io_eff(CID(UDP.try_send_to), udp_try_send_to);
io_eff(CID(UDP.try_send_bytes_to), udp_try_send_bytes_to);
