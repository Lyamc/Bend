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
