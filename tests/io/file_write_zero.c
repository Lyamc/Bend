#ifndef _WIN32
#include <dlfcn.h>

// Every nonempty write to stall_fd makes no progress; the rest go through.
static int stall_fd = -1;

ssize_t write(int fd, const void* data, size_t size) {
  if (fd == stall_fd && size > 0) {
    return 0;
  }
  ssize_t (*real)(int, const void*, size_t) = dlsym(RTLD_NEXT, "write");
  return real(fd, data, size);
}

#else

// The Windows runtime routes write through win_write, which asks
// win_on_write first; answering 0 progress there is the same stall.
static int stall_fd = -1;

static int stall_write(int fd, const void* data, size_t size, ssize_t* out) {
  (void)data;
  if (fd == stall_fd && size > 0) {
    *out = 0;
    return 1;
  }
  return 0;
}

#endif

Term stall_open_run(Env e, Term* f, IoWork* w) {
  (void)f;
  (void)w;
  stall_fd = open("/dev/null", O_WRONLY);
#ifdef _WIN32
  win_on_write = stall_write;
#endif
  return stall_fd < 0 ? io_fail(e, (u32)errno, NULL)
    : io_done(e, io_hand(stall_fd));
}

static void __attribute__((constructor)) stall_use(void) {
  io_eff(CID(Stall.open), stall_open_run);
}
