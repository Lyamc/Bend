#ifndef _WIN32
#include <dlfcn.h>
#endif

static int interrupted_armed;
static u32 interrupted_calls;

#ifndef _WIN32
// A signal that cuts a connect leaves it going on in the background: start
// the real connect, then answer EINTR as the interrupted call would.
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
  int (*real)(int, const struct sockaddr*, socklen_t) = dlsym(RTLD_NEXT, "connect");
  int r = real(fd, addr, len);
  if (interrupted_armed && interrupted_calls++ == 0
    && (r == 0 || errno == EINPROGRESS)) {
    errno = EINTR;
    return -1;
  }
  return r;
}
#else
static int interrupted_connect(int fd, const struct sockaddr* addr, int len,
  int* out) {
  int (*saved)(int, const struct sockaddr*, int, int*) = win_on_connect;
  win_on_connect = NULL;
  int r = win_connect(fd, addr, len);
  win_on_connect = saved;
  if (interrupted_armed && interrupted_calls++ == 0
    && (r == 0 || errno == EINPROGRESS)) {
    errno = EINTR;
    *out = -1;
    return 1;
  }
  *out = r;
  return 1;
}
#endif

Term interrupted_port_run(Env e, Term* f, IoWork* w) {
  int fd = (int)io_hand_v(f[0]);
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getsockname(fd, (struct sockaddr*)&addr, &len)) {
    err_fail("getsockname failed");
  }
  return io_tup(e, f[0], ntohs(addr.sin_port));
}

Term interrupted_arm_run(Env e, Term* f, IoWork* w) {
  interrupted_armed = 1;
  interrupted_calls = 0;
#ifdef _WIN32
  win_on_connect = interrupted_connect;
#endif
  return f[0];
}

// One call: an interrupted connect must not be issued again.
Term interrupted_check_run(Env e, Term* f, IoWork* w) {
  if (interrupted_calls != 1) {
    err_fail("the interrupted connect was issued again");
  }
  interrupted_armed = 0;
#ifdef _WIN32
  win_on_connect = NULL;
#endif
  return f[0];
}

static void __attribute__((constructor)) interrupted_use(void) {
  io_eff(CID(Interrupted.port), interrupted_port_run);
  io_eff(CID(Interrupted.arm), interrupted_arm_run);
  io_eff(CID(Interrupted.check), interrupted_check_run);
}
