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

// TCP
// ===

Term tcp_listen_run(Env e, Term* f, IoWork* w) {
  IoAddr at;
  socklen_t alen = 0;
  u64 len;
  char* host = io_cstr(e, f[0], &len);
  int valid = !io_nul(host, len)
    && io_addr_parse(host, (u32)f[1], &at, &alen) == 0;
  free(host);
  if (!valid) {
    return io_fail(e, EINVAL, NULL);
  }
  int fd = socket(at.v4.sin_family, SOCK_STREAM, 0);
  if (fd < 0) {
    return io_fail(e, (uint32_t)errno, NULL);
  }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  int bound = bind(fd, (struct sockaddr*)&at, alen);
  if (bound < 0 || listen(fd, 512) < 0
    || fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
    uint32_t code = (uint32_t)errno;
    close(fd);
    return io_fail(e, code, NULL);
  }
  return io_done(e, io_hand(fd));
}

static void __attribute__((constructor)) tcp_listen_use(void) {
  io_eff(CID(TCP.listen), tcp_listen_run);
}
