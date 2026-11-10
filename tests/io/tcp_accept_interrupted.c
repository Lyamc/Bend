#ifndef _WIN32
#include <dlfcn.h>
#endif

static int interrupted_fd = -1;
static u32 interrupted_stage;

#ifndef _WIN32
// Interrupt the first accept, then let the retry accept for real.
int accept(int fd, struct sockaddr* addr, socklen_t* len) {
  int (*real)(int, struct sockaddr*, socklen_t*) = dlsym(RTLD_NEXT, "accept");
  if (fd == interrupted_fd && interrupted_stage++ == 0) {
    errno = EINTR;
    return -1;
  }
  return real(fd, addr, len);
}
#else
static int interrupted_accept(int fd, struct sockaddr* addr, int* len, int* out) {
  (void)addr;
  (void)len;
  if (fd == interrupted_fd && interrupted_stage++ == 0) {
    errno = EINTR;
    *out = -1;
    return 1;
  }
  return 0;
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
  interrupted_fd = (int)io_hand_v(f[0]);
  interrupted_stage = 0;
#ifdef _WIN32
  win_on_accept = interrupted_accept;
#endif
  return f[0];
}

Term interrupted_check_run(Env e, Term* f, IoWork* w) {
  if (interrupted_stage != 2) {
    err_fail("the accept did not retry after its interruption");
  }
  interrupted_fd = -1;
#ifdef _WIN32
  win_on_accept = NULL;
#endif
  return f[0];
}

static void __attribute__((constructor)) interrupted_use(void) {
  io_eff(CID(Interrupted.port), interrupted_port_run);
  io_eff(CID(Interrupted.arm), interrupted_arm_run);
  io_eff(CID(Interrupted.check), interrupted_check_run);
}
