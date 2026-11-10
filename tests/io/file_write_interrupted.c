#ifndef _WIN32
#include <dlfcn.h>

static int interrupted_fd = -1;
static u32 interrupted_stage;

// Interrupt before and after a short write to one file.
ssize_t write(int fd, const void* data, size_t size) {
  ssize_t (*real)(int, const void*, size_t) = dlsym(RTLD_NEXT, "write");
  if (fd == interrupted_fd && size > 0) {
    u32 stage = interrupted_stage++;
    if (stage == 0 || stage == 2) {
      errno = EINTR;
      return -1;
    }
    if (stage == 1 && size > 2) {
      size = 2;
    }
  }
  return real(fd, data, size);
}

#else

static int interrupted_fd = -1;
static u32 interrupted_stage;

static int interrupted_write(int fd, const void* data, size_t size, ssize_t* out) {
  (void)data;
  if (fd == interrupted_fd && size > 0) {
    u32 stage = interrupted_stage++;
    if (stage == 0 || stage == 2) {
      errno = EINTR;
      *out = -1;
      return 1;
    }
    if (stage == 1 && size > 2) {
      *out = _write(fd, data, 2);
      return 1;
    }
  }
  return 0;
}

#endif

Term interrupted_arm_run(Env e, Term* f, IoWork* w) {
#ifdef _WIN32
  win_on_write = interrupted_write;
#endif
  interrupted_fd = (int)io_hand_v(f[0]);
  interrupted_stage = 0;
  return f[0];
}

Term interrupted_check_run(Env e, Term* f, IoWork* w) {
  if (interrupted_stage != 4) {
    err_fail("the write did not run every interruption stage");
  }
  interrupted_fd = -1;
#ifdef _WIN32
  win_on_write = NULL;
#endif
  return f[0];
}

static void __attribute__((constructor)) interrupted_use(void) {
  io_eff(CID(Interrupted.arm), interrupted_arm_run);
  io_eff(CID(Interrupted.check), interrupted_check_run);
}
