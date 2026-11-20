// Numeric host parsing shared by the TCP and UDP effects. Effect sources
// are inlined once per import path, so the guard keeps a single definition
// when a program uses several of these effects.
#ifndef BEND_IO_ADDR_C
#define BEND_IO_ADDR_C

typedef union { struct sockaddr_in v4; struct sockaddr_in6 v6; } IoAddr;

// A colon selects IPv6, including "::ffff:1.2.3.4". Names stay refused:
// the IO loop cannot wait on DNS. Callers use sin_family for socket() and
// alen for bind/connect/sendto.
static int io_addr_parse(const char* h, u32 port, IoAddr* at,
  socklen_t* alen) {
  memset(at, 0, sizeof *at);
  if (port > 65535) {
    return -1;
  }
  if (strchr(h, ':') != NULL) {
    at->v6.sin6_family = AF_INET6;
    at->v6.sin6_port = htons((uint16_t)port);
    if (inet_pton(AF_INET6, h, &at->v6.sin6_addr) != 1) {
      return -1;
    }
    *alen = sizeof(struct sockaddr_in6);
    return 0;
  }
  for (const char* p = h; *p != 0; p += 1) {
    if ((p == h || p[-1] == '.') && *p == '0'
      && p[1] >= '0' && p[1] <= '9') {
      return -1;
    }
  }
  at->v4.sin_family = AF_INET;
  at->v4.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, h, &at->v4.sin_addr) != 1) {
    return -1;
  }
  *alen = sizeof(struct sockaddr_in);
  return 0;
}

#endif

// UDP
// ===

// The request parks until the socket is readable, so a backlog never keeps
// the loop from its timers; a recv that still finds no datagram (the socket
// is non-blocking) parks again on more. The datagram, read makes a String
// (io_str) or a List of bytes (io_list).
static Term udp_recv_from_with(Env e, IoWork* w, IoPack more,
  Term (*read)(Env, const char*, u64)) {
  IoAddr at = { 0 };
  socklen_t alen = sizeof at;
  char      host[INET6_ADDRSTRLEN];
  int       fd = (int)w->hand;
  ssize_t   n;
  do {
    n = recvfrom(fd, w->data, (size_t)w->made, 0, (struct sockaddr*)&at,
      &alen);
  } while (n < 0 && errno == EINTR);
  w->size = io_sys_end(w, n);
  if (io_again(w)) {
    return io_wait_on(w, fd, POLLIN, w->time, more);
  }
  int v6 = at.v4.sin_family == AF_INET6;
  const void* addr = v6 ? (const void*)&at.v6.sin6_addr
    : (const void*)&at.v4.sin_addr;
  if (inet_ntop(v6 ? AF_INET6 : AF_INET, addr, host, sizeof host) == NULL) {
    host[0] = 0;
  }
  u32 port = ntohs(v6 ? at.v6.sin6_port : at.v4.sin_port);
  Term r = io_poll_end(e, w, term_pak(CID(Unit), 0), io_res(e, w,
    io_tup(e, io_str(e, host, strlen(host)),
      io_tup(e, port, read(e, w->data, w->size)))));
  free(w->data);
  return io_tup(e, io_hand(w->hand), r);
}

// at is the try_ deadline (past it, Wait{}), 0 for the blocking twins.
static Term udp_recv_from_start(Env e, Term* f, IoWork* w, IoPack more, u64 at) {
  w->hand = (intptr_t)io_hand_v(f[0]);
  w->time = at;
  if (f[1] == 0) {
    w->code = EINVAL;
    return io_tup(e, io_hand(w->hand), io_poll_end(e, w,
      term_pak(CID(Unit), 0), io_fail(e, EINVAL, NULL)));
  }
  // IPv4's 65535-byte packet has at least 20 IP and 8 UDP header bytes.
  w->made = f[1] < 65507 ? (intptr_t)f[1] : 65507;
  w->data = io_mem(malloc((size_t)w->made + 1));
  return io_wait_on(w, (int)w->hand, POLLIN, at, more);
}

static Term udp_recv_from_more(Env e, IoWork* w) {
  return udp_recv_from_with(e, w, udp_recv_from_more, io_str);
}

#ifdef CID(Con)

static Term udp_recv_bytes_from_more(Env e, IoWork* w) {
  return udp_recv_from_with(e, w, udp_recv_bytes_from_more, io_list);
}

#endif

#ifdef CID(UDP.recv_from)

Term udp_recv_from_run(Env e, Term* f, IoWork* w) {
  return udp_recv_from_start(e, f, w, udp_recv_from_more, 0);
}

static void __attribute__((constructor)) udp_recv_from_use(void) {
  io_eff(CID(UDP.recv_from), udp_recv_from_run);
}

#endif

#ifdef CID(UDP.try_recv_from)

Term udp_try_recv_from_run(Env e, Term* f, IoWork* w) {
  return udp_recv_from_start(e, f, w, udp_recv_from_more, io_until(f[2]));
}

static void __attribute__((constructor)) udp_try_recv_from_use(void) {
  io_eff(CID(UDP.try_recv_from), udp_try_recv_from_run);
}

#endif

#ifdef CID(UDP.recv_bytes_from)

Term udp_recv_bytes_from_run(Env e, Term* f, IoWork* w) {
  return udp_recv_from_start(e, f, w, udp_recv_bytes_from_more, 0);
}

static void __attribute__((constructor)) udp_recv_bytes_from_use(void) {
  io_eff(CID(UDP.recv_bytes_from), udp_recv_bytes_from_run);
}

#endif

#ifdef CID(UDP.try_recv_bytes_from)

Term udp_try_recv_bytes_from_run(Env e, Term* f, IoWork* w) {
  return udp_recv_from_start(e, f, w, udp_recv_bytes_from_more,
    io_until(f[2]));
}

static void __attribute__((constructor)) udp_try_recv_bytes_from_use(void) {
  io_eff(CID(UDP.try_recv_bytes_from), udp_try_recv_bytes_from_run);
}

#endif
