// Native Windows stand-in for the POSIX headers the Bend C runtime uses.
// Included only when compiling the generated program on _WIN32.
#ifndef BEND_WINPOSIX_H
#define BEND_WINPOSIX_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <malloc.h>
#include <bcrypt.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef ENOTSUP
#define ENOTSUP 129
#endif
#ifndef ENODEV
#define ENODEV 19
#endif
#ifndef ENXIO
#define ENXIO 6
#endif
#ifndef EINPROGRESS
#define EINPROGRESS 115
#endif
#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 132
#endif

#ifndef O_RDONLY
#define O_RDONLY _O_RDONLY
#endif
#ifndef O_WRONLY
#define O_WRONLY _O_WRONLY
#endif
#ifndef O_RDWR
#define O_RDWR _O_RDWR
#endif
#ifndef O_CREAT
#define O_CREAT _O_CREAT
#endif
#ifndef O_TRUNC
#define O_TRUNC _O_TRUNC
#endif
#ifndef O_APPEND
#define O_APPEND _O_APPEND
#endif
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#define O_NONBLOCK 0x800

#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#endif

typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef HANDLE pthread_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT
#define pthread_mutex_lock(m) AcquireSRWLockExclusive(m)
#define pthread_mutex_unlock(m) ReleaseSRWLockExclusive(m)
#define pthread_cond_wait(c, m) SleepConditionVariableSRW(c, m, INFINITE, 0)
#define pthread_cond_broadcast(c) WakeAllConditionVariable(c)
#define pthread_cond_signal(c) WakeConditionVariable(c)

typedef struct {
  void* (*fn)(void*);
  void* arg;
} win_thr_arg;

static DWORD WINAPI win_thr_go(LPVOID p) {
  win_thr_arg t = *(win_thr_arg*)p;
  free(p);
  t.fn(t.arg);
  return 0;
}

static int pthread_create(pthread_t* tid, void* attr, void* (*fn)(void*), void* arg) {
  (void)attr;
  win_thr_arg* t = (win_thr_arg*)malloc(sizeof *t);
  if (t == NULL) {
    return 1;
  }
  t->fn = fn;
  t->arg = arg;
  HANDLE h = CreateThread(NULL, 0, win_thr_go, t, 0, NULL);
  if (h == NULL) {
    free(t);
    return 1;
  }
  *tid = h;
  return 0;
}

static int pthread_detach(pthread_t t) {
  CloseHandle(t);
  return 0;
}

static int pthread_join(pthread_t t, void** ret) {
  (void)ret;
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
  return 0;
}

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifndef PROT_READ
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE   0
#define MAP_ANON      0
#define MAP_ANONYMOUS 0
#define MAP_NORESERVE 0
#endif
#define MAP_FAILED ((void*)(intptr_t)-1)

#define SIGPIPE 13
#define SIGBUS  10
#ifndef SIGSTKSZ
#define SIGSTKSZ 65536
#endif
#define SA_ONSTACK 1
#ifndef SIG_IGN
#define SIG_IGN ((void*)1)
#endif

typedef struct {
  void*  ss_sp;
  size_t ss_size;
} stack_t;

struct sigaction {
  void (*sa_handler)(int);
  int sa_flags;
};

#ifdef POLLIN
#undef POLLIN
#undef POLLOUT
#undef POLLERR
#undef POLLHUP
#endif
#define POLLIN  0x001
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define pollfd bend_pollfd
struct bend_pollfd {
  int   fd;
  short events;
  short revents;
};

#define F_GETFL 3
#define F_SETFL 4
#define _SC_NPROCESSORS_ONLN 1

#ifndef ssize_t
typedef intptr_t ssize_t;
#endif

typedef struct win_guard {
  char* start;
  char* end;
  struct win_guard* next;
} win_guard;

static win_guard* win_guards;
static win_guard* win_maps;
static LONG win_once;

static int win_in_list(win_guard* g, void* at) {
  char* p = (char*)at;
  for (; g != NULL; g = g->next) {
    if (p >= g->start && p < g->end) {
      return 1;
    }
  }
  return 0;
}

static int win_is_guard(void* at) {
  return win_in_list(win_guards, at);
}

static int win_is_map(void* at) {
  return win_in_list(win_maps, at);
}

static void win_guard_add(void* p, size_t n) {
  win_guard* g = (win_guard*)malloc(sizeof *g);
  if (g == NULL) {
    return;
  }
  g->start = (char*)p;
  g->end = (char*)p + n;
  g->next = win_guards;
  win_guards = g;
}

static LONG CALLBACK win_veh(PEXCEPTION_POINTERS x) {
  if (x->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  void* at = (void*)x->ExceptionRecord->ExceptionInformation[1];
  if (win_is_guard(at) || !win_is_map(at)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  void* page = (void*)((uintptr_t)at & ~(uintptr_t)65535);
  if (VirtualAlloc(page, 65536, MEM_COMMIT, PAGE_READWRITE) != NULL) {
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void win_init(void) {
  if (InterlockedCompareExchange(&win_once, 1, 0) != 0) {
    return;
  }
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
  AddVectoredExceptionHandler(1, win_veh);
}

static int win_wsa_errno(void) {
  int e = WSAGetLastError();
  switch (e) {
    case WSAEWOULDBLOCK: return EAGAIN;
    case WSAEINPROGRESS: return EINPROGRESS;
    case WSAECONNRESET:  return WSAECONNRESET;
    case WSAECONNREFUSED: return WSAECONNREFUSED;
    case WSAETIMEDOUT:   return WSAETIMEDOUT;
    case WSAEADDRINUSE:  return WSAEADDRINUSE;
    default: return e ? e : EINVAL;
  }
}

static void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
  (void)addr;
  (void)prot;
  (void)flags;
  (void)off;
  win_init();
  if (fd >= 0) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    HANDLE m = CreateFileMappingW(h, NULL, PAGE_READONLY, 0, 0, NULL);
    if (m == NULL) {
      return MAP_FAILED;
    }
    void* p = MapViewOfFile(m, FILE_MAP_READ, 0, 0, len);
    CloseHandle(m);
    return p != NULL ? p : MAP_FAILED;
  }
  void* p = VirtualAlloc(NULL, len, MEM_RESERVE, PAGE_READWRITE);
  if (p == NULL) {
    return MAP_FAILED;
  }
  win_guard* g = (win_guard*)malloc(sizeof *g);
  if (g != NULL) {
    g->start = (char*)p;
    g->end = (char*)p + len;
    g->next = win_maps;
    win_maps = g;
  }
  return p;
}

static int mprotect(void* p, size_t n, int prot) {
  if (prot == PROT_NONE) {
    if (VirtualAlloc(p, n, MEM_COMMIT, PAGE_NOACCESS) == NULL) {
      return -1;
    }
    win_guard_add(p, n);
    return 0;
  }
  DWORD old;
  return VirtualProtect(p, n, PAGE_READWRITE, &old) ? 0 : -1;
}

static int clock_gettime(int clk, struct timespec* ts) {
  (void)clk;
  static LARGE_INTEGER f;
  LARGE_INTEGER c;
  if (f.QuadPart == 0) {
    QueryPerformanceFrequency(&f);
  }
  QueryPerformanceCounter(&c);
  ts->tv_sec = (time_t)(c.QuadPart / f.QuadPart);
  ts->tv_nsec = (long)((c.QuadPart % f.QuadPart) * 1000000000ull / (unsigned long long)f.QuadPart);
  return 0;
}

static int nanosleep(const struct timespec* ts, struct timespec* rem) {
  (void)rem;
  DWORD ms = (DWORD)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
  if (ms == 0 && (ts->tv_sec > 0 || ts->tv_nsec > 0)) {
    ms = 1;
  }
  Sleep(ms);
  return 0;
}

static long sysconf(int name) {
  (void)name;
  SYSTEM_INFO i;
  GetSystemInfo(&i);
  return (long)i.dwNumberOfProcessors;
}

static void* win_signal(int sig, void* h) {
  (void)sig;
  return h;
}
#ifdef signal
#undef signal
#endif
#define signal win_signal

static int sigaltstack(stack_t* ss, void* old) {
  (void)ss;
  (void)old;
  return 0;
}

static int sigaction(int sig, const struct sigaction* a, void* old) {
  (void)sig;
  (void)a;
  (void)old;
  return 0;
}

static int readlink(const char* path, char* buf, size_t n) {
  if (strcmp(path, "/proc/self/exe") != 0) {
    errno = EINVAL;
    return -1;
  }
  DWORD k = GetModuleFileNameA(NULL, buf, (DWORD)n);
  return k > 0 && k < n ? (int)k : -1;
}

static int setenv(const char* k, const char* v, int overwrite) {
  if (!overwrite && getenv(k) != NULL) {
    return 0;
  }
  return _putenv_s(k, v);
}

static int win_open(const char* path, int flags, ...) {
  int mode = _S_IREAD | _S_IWRITE;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  return _open(path, flags | _O_BINARY, mode);
}

static int win_close(int fd) {
  return _close(fd);
}

static ssize_t win_read(int fd, void* buf, size_t n) {
  int r = _read(fd, buf, n > INT_MAX ? INT_MAX : (unsigned)n);
  return r;
}

static ssize_t win_write(int fd, const void* buf, size_t n) {
  int r = _write(fd, buf, n > INT_MAX ? INT_MAX : (unsigned)n);
  return r;
}

static SOCKET win_sok(int fd) {
  return (SOCKET)_get_osfhandle(fd);
}

static int win_sockfd(SOCKET s) {
  if (s == INVALID_SOCKET) {
    errno = win_wsa_errno();
    return -1;
  }
  int fd = _open_osfhandle((intptr_t)s, _O_RDWR);
  if (fd < 0) {
    closesocket(s);
    errno = EMFILE;
    return -1;
  }
  return fd;
}

static int win_socket(int domain, int type, int proto) {
  win_init();
  return win_sockfd(WSASocketW(domain, type, proto, NULL, 0, 0));
}

static int win_bind(int fd, const struct sockaddr* a, int n) {
  if (bind(win_sok(fd), a, n) == 0) {
    return 0;
  }
  errno = win_wsa_errno();
  return -1;
}

static int win_listen(int fd, int backlog) {
  if (listen(win_sok(fd), backlog) == 0) {
    return 0;
  }
  errno = win_wsa_errno();
  return -1;
}

static int win_connect(int fd, const struct sockaddr* a, int n) {
  if (connect(win_sok(fd), a, n) == 0) {
    return 0;
  }
  int e = WSAGetLastError();
  errno = e == WSAEWOULDBLOCK ? EINPROGRESS : win_wsa_errno();
  return -1;
}

static int win_accept(int fd, struct sockaddr* a, int* n) {
  SOCKET s = accept(win_sok(fd), a, n);
  if (s == INVALID_SOCKET) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return win_sockfd(s);
}

static ssize_t win_send(int fd, const void* buf, size_t n, int flags) {
  int r = send(win_sok(fd), (const char*)buf, n > INT_MAX ? INT_MAX : (int)n, flags);
  if (r < 0) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return r;
}

static ssize_t win_recv(int fd, void* buf, size_t n, int flags) {
  int r = recv(win_sok(fd), (char*)buf, n > INT_MAX ? INT_MAX : (int)n, flags);
  if (r < 0) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return r;
}

static ssize_t win_sendto(int fd, const void* buf, size_t n, int flags,
  const struct sockaddr* a, int alen) {
  int r = sendto(win_sok(fd), (const char*)buf, n > INT_MAX ? INT_MAX : (int)n,
    flags, a, alen);
  if (r < 0) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return r;
}

static ssize_t win_recvfrom(int fd, void* buf, size_t n, int flags,
  struct sockaddr* a, int* alen) {
  int r = recvfrom(win_sok(fd), (char*)buf, n > INT_MAX ? INT_MAX : (int)n,
    flags, a, alen);
  if (r < 0) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return r;
}

static int win_setsockopt(int fd, int level, int name, const void* v, int n) {
  if (setsockopt(win_sok(fd), level, name, (const char*)v, n) == 0) {
    return 0;
  }
  errno = win_wsa_errno();
  return -1;
}

static int win_getsockopt(int fd, int level, int name, void* v, int* n) {
  if (getsockopt(win_sok(fd), level, name, (char*)v, n) == 0) {
    if (name == SO_ERROR) {
      int* e = (int*)v;
      if (*e == WSAEWOULDBLOCK) {
        *e = EINPROGRESS;
      } else if (*e == 0) {
        *e = 0;
      } else {
        WSASetLastError(*e);
        *e = win_wsa_errno();
      }
    }
    return 0;
  }
  errno = win_wsa_errno();
  return -1;
}

static int fcntl(int fd, int cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  int arg = va_arg(ap, int);
  va_end(ap);
  if (cmd == F_GETFL) {
    return 0;
  }
  if (cmd == F_SETFL) {
    u_long nb = (arg & O_NONBLOCK) ? 1 : 0;
    if (ioctlsocket(win_sok(fd), FIONBIO, &nb) == 0) {
      return 0;
    }
    errno = win_wsa_errno();
    return -1;
  }
  errno = EINVAL;
  return -1;
}

static int poll(struct pollfd* fds, unsigned n, int ms) {
  win_init();
  WSAPOLLFD* w = (WSAPOLLFD*)calloc(n, sizeof *w);
  if (w == NULL) {
    errno = ENOMEM;
    return -1;
  }
  for (unsigned i = 0; i < n; i += 1) {
    w[i].fd = win_sok(fds[i].fd);
    w[i].events = 0;
    if (fds[i].events & POLLIN) {
      w[i].events |= POLLRDNORM;
    }
    if (fds[i].events & POLLOUT) {
      w[i].events |= POLLWRNORM;
    }
    w[i].revents = 0;
  }
  int r = WSAPoll(w, n, ms);
  if (r < 0) {
    errno = win_wsa_errno();
    free(w);
    return -1;
  }
  for (unsigned i = 0; i < n; i += 1) {
    fds[i].revents = 0;
    if (w[i].revents & (POLLRDNORM | POLLHUP | POLLERR | POLLIN)) {
      fds[i].revents |= POLLIN;
    }
    if (w[i].revents & POLLWRNORM) {
      fds[i].revents |= POLLOUT;
    }
    if (w[i].revents & POLLERR) {
      fds[i].revents |= POLLERR;
    }
  }
  free(w);
  return r;
}

static int pipe(int fds[2]) {
  win_init();
  SOCKET li = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (li == INVALID_SOCKET || bind(li, (struct sockaddr*)&a, sizeof a) != 0
    || listen(li, 1) != 0) {
    if (li != INVALID_SOCKET) {
      closesocket(li);
    }
    errno = win_wsa_errno();
    return -1;
  }
  int alen = sizeof a;
  getsockname(li, (struct sockaddr*)&a, &alen);
  SOCKET c = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
  if (c == INVALID_SOCKET || connect(c, (struct sockaddr*)&a, sizeof a) != 0) {
    closesocket(li);
    if (c != INVALID_SOCKET) {
      closesocket(c);
    }
    errno = win_wsa_errno();
    return -1;
  }
  SOCKET s = accept(li, NULL, NULL);
  closesocket(li);
  if (s == INVALID_SOCKET) {
    closesocket(c);
    errno = win_wsa_errno();
    return -1;
  }
  u_long nb = 1;
  ioctlsocket(c, FIONBIO, &nb);
  ioctlsocket(s, FIONBIO, &nb);
  fds[0] = win_sockfd(s);
  fds[1] = win_sockfd(c);
  if (fds[0] < 0 || fds[1] < 0) {
    return -1;
  }
  return 0;
}

#ifdef socket
#undef socket
#endif
#ifdef bind
#undef bind
#endif
#ifdef listen
#undef listen
#endif
#ifdef connect
#undef connect
#endif
#ifdef accept
#undef accept
#endif
#ifdef send
#undef send
#endif
#ifdef recv
#undef recv
#endif
#ifdef sendto
#undef sendto
#endif
#ifdef recvfrom
#undef recvfrom
#endif
#ifdef setsockopt
#undef setsockopt
#endif
#ifdef getsockopt
#undef getsockopt
#endif
#ifdef close
#undef close
#endif
#ifdef open
#undef open
#endif
#ifdef read
#undef read
#endif
#ifdef write
#undef write
#endif

#define socket     win_socket
#define bind       win_bind
#define listen     win_listen
#define connect    win_connect
#define accept     win_accept
#define send       win_send
#define recv       win_recv
#define sendto     win_sendto
#define recvfrom   win_recvfrom
#define setsockopt win_setsockopt
#define getsockopt win_getsockopt
static ssize_t pread(int fd, void* buf, size_t n, off_t off) {
  __int64 cur = _telli64(fd);
  if (cur < 0 || _lseeki64(fd, off, SEEK_SET) < 0) {
    return -1;
  }
  int r = _read(fd, buf, n > INT_MAX ? INT_MAX : (unsigned)n);
  _lseeki64(fd, cur, SEEK_SET);
  return r;
}

static ssize_t getrandom(void* buf, size_t n, unsigned flags) {
  (void)flags;
  NTSTATUS st = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n,
    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (st != 0) {
    errno = EIO;
    return -1;
  }
  return (ssize_t)n;
}

#define close      win_close
#define open       win_open
#define read       win_read
#define write      win_write

#define fstat _fstat64
#define stat  _stat64

#ifdef FAR
#undef FAR
#endif

#endif
