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

// The socket is non-blocking for life. The connect's outcome is w->code:
// EINPROGRESS parked the computation until the socket was writable, and
// then SO_ERROR says how it ended.
static Term tcp_connect_more(Env e, IoWork* w) {
  int       fd  = (int)w->made;
  int       err = (int)w->code;
  socklen_t len = sizeof(err);
  if (err == EINPROGRESS && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len)) {
    err = errno;
  }
  if (err != 0 && fd >= 0) {
    close(fd);
  }
  free(w->data);
  return err != 0 ? io_fail(e, (u32)err, NULL) : io_done(e, io_hand(fd));
}

Term tcp_connect_run(Env e, Term* f, IoWork* w) {
  IoAddr at;
  socklen_t alen = 0;
  w->data = io_cstr(e, f[0], &w->size);
  int fd  = -1;
  errno   = EINVAL;
  if (!io_nul(w->data, w->size)
    && io_addr_parse(w->data, (u32)f[1], &at, &alen) == 0) {
    fd = socket(at.v4.sin_family, SOCK_STREAM, 0);
  }
  if (fd >= 0 && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
    close(fd);
    fd = -1;
  }
  w->made = fd;
  io_sys_end(w, fd < 0 ? fd : connect(fd, (struct sockaddr*)&at, alen));
  // An interrupted connect goes on in the background, so it is waited on
  // as EINPROGRESS is; calling connect again would answer EALREADY.
  if (fd >= 0 && w->code == EINTR) {
    w->code = EINPROGRESS;
  }
  return w->code == EINPROGRESS
    ? io_wait_on(w, fd, POLLOUT, 0, tcp_connect_more) : tcp_connect_more(e, w);
}

static void __attribute__((constructor)) tcp_connect_use(void) {
  io_eff(CID(TCP.connect), tcp_connect_run);
}
