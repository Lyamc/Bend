// Native Windows stand-in for the POSIX headers the Bend C runtime uses.
// comp.ts splices this text into generated C under _WIN32. It is not an
// effect: nothing imports it.
#ifndef BEND_WINPOSIX_H
#define BEND_WINPOSIX_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
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
#ifndef O_EXCL
#define O_EXCL _O_EXCL
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
#ifndef SA_NODEFER
#define SA_NODEFER 4
#endif
#ifndef SIG_IGN
#define SIG_IGN ((void*)1)
#endif
#ifndef SIG_DFL
#define SIG_DFL ((void (*)(int))0)
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
  switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK: return 11;
    case WSAEINPROGRESS: return 115;
    case WSAEALREADY: return 114;
    case WSAENOTSOCK: return 88;
    case WSAEDESTADDRREQ: return 89;
    case WSAEMSGSIZE: return 90;
    case WSAEPROTOTYPE: return 91;
    case WSAENOPROTOOPT: return 92;
    case WSAEPROTONOSUPPORT: return 93;
    case WSAESOCKTNOSUPPORT: return 94;
    case WSAEOPNOTSUPP: return 95;
    case WSAEPFNOSUPPORT: return 96;
    case WSAEAFNOSUPPORT: return 97;
    case WSAEADDRINUSE: return 98;
    case WSAEADDRNOTAVAIL: return 99;
    case WSAENETDOWN: return 100;
    case WSAENETUNREACH: return 101;
    case WSAECONNABORTED: return 103;
    case WSAECONNRESET: return 104;
    case WSAENOBUFS: return 105;
    case WSAEISCONN: return 106;
    case WSAENOTCONN: return 107;
    case WSAESHUTDOWN: return 108;
    case WSAETIMEDOUT: return 110;
    case WSAECONNREFUSED: return 111;
    case WSAEHOSTDOWN: return 112;
    case WSAEHOSTUNREACH: return 113;
    case WSAEINVAL: return 22;
    case WSAEACCES: return 13;
    case WSAEFAULT: return 14;
    case WSAEMFILE: return 24;
    default: return 5;
  }
}

static void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
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
  void* p = VirtualAlloc(addr, len, MEM_RESERVE, PAGE_READWRITE);
  if (p == NULL || (addr != NULL && p != addr)) {
    if (p != NULL && p != addr) {
      VirtualFree(p, 0, MEM_RELEASE);
    }
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

static int munmap(void* p, size_t n) {
  (void)n;
  if (UnmapViewOfFile(p)) {
    return 0;
  }
  return VirtualFree(p, 0, MEM_RELEASE) ? 0 : -1;
}

static SOCKET win_sok(int fd);
static ssize_t win_send(int fd, const void* buf, size_t n, int flags);
static ssize_t win_recv(int fd, void* buf, size_t n, int flags);
static SOCKET win_wake_r = INVALID_SOCKET;
static SOCKET win_wake_w = INVALID_SOCKET;
static int win_sig_on = 0;
#define WIN_FIFO_BIT 0x40000000u
static unsigned win_file_flags[65536];
static int (*win_on_open)(const char*, int, int, int*);
static int (*win_on_read)(int, void*, size_t, ssize_t*);
static int (*win_on_write)(int, const void*, size_t, ssize_t*);
static int (*win_on_pwrite)(int, const void*, size_t, long long, ssize_t*);
static int (*win_on_send)(int, const void*, size_t, int, ssize_t*);
static int (*win_on_recv)(int, void*, size_t, int, ssize_t*);
static int (*win_on_recvfrom)(int, void*, size_t, int, struct sockaddr*, int*,
  ssize_t*);
static int (*win_on_accept)(int, struct sockaddr*, int*, int*);
static int (*win_on_connect)(int, const struct sockaddr*, int, int*);
static int (*win_on_sendto)(int, const void*, size_t, int, const struct sockaddr*,
  int, ssize_t*);

#ifdef select
#undef select
#endif

static int win_select(int nfds, fd_set* rfds, fd_set* wfds, fd_set* efds,
  struct timeval* tv) {
  (void)efds;
  win_init();
  unsigned char* r = (unsigned char*)rfds;
  unsigned char* w = (unsigned char*)wfds;
  int ms = tv == NULL ? -1
    : (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
  WSAPOLLFD* p = (WSAPOLLFD*)calloc((size_t)nfds + 1, sizeof *p);
  int* map = (int*)calloc((size_t)nfds + 1, sizeof *map);
  if (p == NULL || map == NULL) {
    free(p);
    free(map);
    errno = ENOMEM;
    return -1;
  }
  int n = 0;
  int fifo_fd[64];
  int fifo_n = 0;
  for (int fd = 0; fd < nfds; fd += 1) {
    short ev = 0;
    if (r != NULL && (r[fd / 8] >> (fd % 8) & 1)) {
      ev |= POLLRDNORM;
    }
    if (w != NULL && (w[fd / 8] >> (fd % 8) & 1)) {
      ev |= POLLWRNORM;
    }
    if (ev == 0) {
      continue;
    }
    if ((unsigned)fd < 65536u && (win_file_flags[fd] & WIN_FIFO_BIT)) {
      if (fifo_n < 64) {
        fifo_fd[fifo_n++] = fd | (ev & POLLRDNORM ? 0 : 0x10000);
      }
      continue;
    }
    p[n].fd = win_sok(fd);
    p[n].events = ev;
    map[n] = fd;
    n += 1;
  }
  int wake = -1;
  if (win_wake_r != INVALID_SOCKET) {
    p[n].fd = win_wake_r;
    p[n].events = POLLRDNORM;
    wake = n;
    n += 1;
  }
  int slice = fifo_n == 0 ? ms : (ms < 0 || ms > 10 ? 10 : ms);
  ULONGLONG start = GetTickCount64();
  int ret = 0;
  int ready = 0;
  for (;;) {
  ret = (n == 0 && fifo_n == 0) ? 0 : (n == 0 ? 0 : WSAPoll(p, (ULONG)n, slice));
  int bytes = (nfds + 7) / 8;
  if (r != NULL) {
    memset(r, 0, (size_t)bytes);
  }
  if (w != NULL) {
    memset(w, 0, (size_t)bytes);
  }
  ready = 0;
  if (ret > 0 && wake >= 0
    && (p[wake].revents & (POLLRDNORM | POLLIN | POLLHUP | POLLERR))) {
    char dump[64];
    while (recv(win_wake_r, dump, sizeof dump, 0) > 0) {
    }
    free(p);
    free(map);
    errno = EINTR;
    return -1;
  }
  if (ret > 0) {
    for (int i = 0; i < n; i += 1) {
      if (i == wake) {
        continue;
      }
      int fd = map[i];
      if (p[i].revents & (POLLRDNORM | POLLHUP | POLLERR | POLLIN)) {
        if (r != NULL) {
          r[fd / 8] |= (unsigned char)(1 << (fd % 8));
        }
        ready += 1;
      }
      if (p[i].revents & POLLWRNORM) {
        if (w != NULL) {
          w[fd / 8] |= (unsigned char)(1 << (fd % 8));
        }
        ready += 1;
      }
    }
  }
  for (int i = 0; i < fifo_n; i += 1) {
    int fd = fifo_fd[i] & 0xffff;
    int want_read = (fifo_fd[i] & 0x10000) == 0;
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    int up = 0;
    if (!want_read) {
      up = 1;
    } else {
      DWORD avail = 0;
      if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
        up = avail > 0;
      } else {
        DWORD e = GetLastError();
        up = e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED;
      }
    }
    if (up) {
      if (want_read && r != NULL) {
        r[fd / 8] |= (unsigned char)(1 << (fd % 8));
      }
      if (!want_read && w != NULL) {
        w[fd / 8] |= (unsigned char)(1 << (fd % 8));
      }
      ready += 1;
    }
  }
  if (ready > 0 || fifo_n == 0 || ms == 0) {
    break;
  }
  if (ms > 0 && (int)(GetTickCount64() - start) >= ms) {
    break;
  }
  Sleep(slice > 0 ? (DWORD)slice : 1);
  }
  free(p);
  free(map);
  if (ret < 0 && ready == 0) {
    errno = win_wsa_errno();
    return -1;
  }
  return ready;
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

#if !defined(__MINGW32__)
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
#endif

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

static void win_wake_init(void) {
  if (win_wake_r != INVALID_SOCKET) {
    return;
  }
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
    return;
  }
  int alen = sizeof a;
  getsockname(li, (struct sockaddr*)&a, &alen);
  SOCKET c = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
  if (c == INVALID_SOCKET || connect(c, (struct sockaddr*)&a, sizeof a) != 0) {
    closesocket(li);
    if (c != INVALID_SOCKET) {
      closesocket(c);
    }
    return;
  }
  SOCKET s = accept(li, NULL, NULL);
  closesocket(li);
  if (s == INVALID_SOCKET) {
    closesocket(c);
    return;
  }
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  win_wake_r = s;
  win_wake_w = c;
}

static int sigaction(int sig, const struct sigaction* a, void* old) {
  (void)sig;
  (void)a;
  (void)old;
  win_sig_on = 1;
  win_wake_init();
  return 0;
}

static int pthread_kill(pthread_t t, int sig) {
  (void)t;
  (void)sig;
  if (win_sig_on && win_wake_w != INVALID_SOCKET) {
    send(win_wake_w, "x", 1, 0);
  }
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

#define WIN_FIFOS 32

typedef struct {
  char path[160];
  char name[200];
  int live;
} win_fifo;

static win_fifo win_fifos[WIN_FIFOS];

static void win_fifo_pipe(const char* path, char* out, size_t n) {
  snprintf(out, n, "\\\\.\\pipe\\bend");
  size_t at = strlen(out);
  for (const char* p = path; *p != 0 && at + 1 < n; p += 1) {
    char c = (*p == '/' || *p == '\\' || *p == ':') ? '_' : *p;
    out[at++] = c;
  }
  out[at] = 0;
}

static int win_fifo_id(const char* path) {
  for (int i = 0; i < WIN_FIFOS; i += 1) {
    if (win_fifos[i].live && strcmp(win_fifos[i].path, path) == 0) {
      return i;
    }
  }
  return -1;
}

static void win_epipe_stdio(void) {
  DWORD e = GetLastError();
  if (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA) {
    errno = EPIPE;
  }
  if (errno == EPIPE && getenv("BEND_EPIPE_CHILD") != NULL) {
    ExitProcess(141);
  }
}

static void win_epipe_child(void) {
  const char* ep = getenv("BEND_EPIPE_CHILD");
  if (ep == NULL || ep[0] == 0) {
    return;
  }
  FILE* out = ep[0] == '2' ? stderr : stdout;
  setvbuf(out, NULL, _IONBF, 0);
  if (fwrite("x\n", 1, 2, out) != 2 || fflush(out) != 0) {
    ExitProcess(141);
  }
  ExitProcess(0);
}

static void __attribute__((constructor)) win_epipe_boot(void) {
  win_epipe_child();
}

static int mkfifo(const char* path, int mode) {
  (void)mode;
  if (path == NULL || path[0] == 0) {
    errno = EINVAL;
    return -1;
  }
  if (win_fifo_id(path) >= 0) {
    errno = EEXIST;
    return -1;
  }
  for (int i = 0; i < WIN_FIFOS; i += 1) {
    if (!win_fifos[i].live) {
      snprintf(win_fifos[i].path, sizeof win_fifos[i].path, "%s", path);
      win_fifo_pipe(path, win_fifos[i].name, sizeof win_fifos[i].name);
      win_fifos[i].live = 1;
      return 0;
    }
  }
  errno = ENOSPC;
  return -1;
}

static int win_fifo_unlink(const char* path) {
  int id = win_fifo_id(path);
  if (id >= 0) {
    win_fifos[id].live = 0;
    return 0;
  }
  return _unlink(path);
}

#define unlink win_fifo_unlink

static int win_fifo_fd(HANDLE h, int flags) {
  int fd = _open_osfhandle((intptr_t)h, flags & O_WRONLY ? 0 : _O_RDONLY);
  if (fd < 0) {
    CloseHandle(h);
    errno = EMFILE;
    return -1;
  }
  if ((unsigned)fd < 65536u) {
    win_file_flags[fd] = (unsigned)flags | WIN_FIFO_BIT;
  }
  return fd;
}

static int win_fifo_open(const char* name, int flags) {
  int wr = (flags & O_WRONLY) != 0 || (flags & O_RDWR) != 0;
  if (!wr) {
    HANDLE h = CreateNamedPipeA(name,
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
      65536, 65536, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
      errno = EIO;
      return -1;
    }
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    BOOL ok = ConnectNamedPipe(h, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      if ((flags & O_NONBLOCK) == 0) {
        WaitForSingleObject(ov.hEvent, INFINITE);
        CloseHandle(ov.hEvent);
      }
    } else {
      CloseHandle(ov.hEvent);
      if (!ok && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(h);
        errno = EIO;
        return -1;
      }
    }
    return win_fifo_fd(h, flags);
  }
  HANDLE h = INVALID_HANDLE_VALUE;
  for (int i = 0; i < 200 && h == INVALID_HANDLE_VALUE; i += 1) {
    h = CreateFileA(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
      Sleep(5);
    }
  }
  if (h == INVALID_HANDLE_VALUE) {
    errno = ENOENT;
    return -1;
  }
  return win_fifo_fd(h, flags);
}

static int win_fifo_err(DWORD e, int writing) {
  if (writing && (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA)) {
    errno = EPIPE;
    return -1;
  }
  if (e == ERROR_NO_DATA || e == ERROR_PIPE_LISTENING) {
    errno = EAGAIN;
    return -1;
  }
  if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED) {
    return 0;
  }
  errno = EIO;
  return -1;
}

static ssize_t win_fifo_io(HANDLE h, void* buf, size_t n, int writing, int block) {
  OVERLAPPED ov;
  memset(&ov, 0, sizeof ov);
  ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  DWORD got = 0;
  BOOL ok = writing
    ? WriteFile(h, buf, n > 65536 ? 65536 : (DWORD)n, &got, &ov)
    : ReadFile(h, buf, n > 65536 ? 65536 : (DWORD)n, &got, &ov);
  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    if (!block) {
      CancelIoEx(h, &ov);
      WaitForSingleObject(ov.hEvent, 50);
      CloseHandle(ov.hEvent);
      errno = EAGAIN;
      return -1;
    }
    WaitForSingleObject(ov.hEvent, INFINITE);
    ok = GetOverlappedResult(h, &ov, &got, FALSE);
  }
  DWORD e = ok ? 0 : GetLastError();
  CloseHandle(ov.hEvent);
  if (ok) {
    return (ssize_t)got;
  }
  if (!writing && (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED)) {
    return 0;
  }
  return win_fifo_err(e, writing);
}

static ssize_t win_fifo_read(int fd, void* buf, size_t n) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  int block = (win_file_flags[fd] & O_NONBLOCK) == 0;
  if (!block) {
    DWORD avail = 0;
    if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
      DWORD e = GetLastError();
      if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED) {
        return 0;
      }
      return win_fifo_err(e, 0);
    }
    if (avail == 0) {
      errno = EAGAIN;
      return -1;
    }
    if (avail < n) {
      n = avail;
    }
  }
  return win_fifo_io(h, buf, n, 0, 1);
}

static ssize_t win_fifo_write(int fd, const void* buf, size_t n) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  DWORD got = 0;
  if (WriteFile(h, buf, n > 65536 ? 65536 : (DWORD)n, &got, NULL)) {
    return (ssize_t)got;
  }
  return win_fifo_err(GetLastError(), 1);
}

static int win_open(const char* path, int flags, ...) {
  int mode = _S_IREAD | _S_IWRITE;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  if (win_on_open != NULL) {
    int out = 0;
    if (win_on_open(path, flags, mode, &out)) {
      return out;
    }
  }
  int fifo = win_fifo_id(path);
  if (fifo >= 0) {
    return win_fifo_open(win_fifos[fifo].name, flags);
  }
  if (path != NULL && strcmp(path, "/dev/null") == 0) {
    if (flags & O_EXCL) {
      errno = EEXIST;
      return -1;
    }
    path = "NUL";
  }
  int fd = _open(path, flags | _O_BINARY, mode);
  if (fd >= 0 && (unsigned)fd < 65536u) {
    win_file_flags[fd] = (unsigned)flags;
  }
  return fd;
}

static int win_close(int fd) {
  return _close(fd);
}

static int win_is_sock(int fd) {
  SOCKET s = (SOCKET)_get_osfhandle(fd);
  int t = 0;
  int n = sizeof t;
  return s != (SOCKET)INVALID_HANDLE_VALUE && s != INVALID_SOCKET
    && getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&t, &n) == 0;
}

static ssize_t win_read(int fd, void* buf, size_t n) {
  if (win_on_read != NULL) {
    ssize_t out = 0;
    if (win_on_read(fd, buf, n, &out)) {
      return out;
    }
  }
  if ((unsigned)fd < 65536u && (win_file_flags[fd] & WIN_FIFO_BIT)) {
    return win_fifo_read(fd, buf, n);
  }
  if (win_is_sock(fd)) {
    return win_recv(fd, buf, n, 0);
  }
  return _read(fd, buf, n > INT_MAX ? INT_MAX : (unsigned)n);
}

static ssize_t win_write(int fd, const void* buf, size_t n) {
  if (win_on_write != NULL) {
    ssize_t out = 0;
    if (win_on_write(fd, buf, n, &out)) {
      return out;
    }
  }
  if ((unsigned)fd < 65536u && (win_file_flags[fd] & WIN_FIFO_BIT)) {
    return win_fifo_write(fd, buf, n);
  }
  if (win_is_sock(fd)) {
    return win_send(fd, buf, n, 0);
  }
  return _write(fd, buf, n > INT_MAX ? INT_MAX : (unsigned)n);
}

static int win_dup2(int from, int to) {
  if (to > 512) {
    _setmaxstdio(to + 1 > 8192 ? 8192 : to + 1);
  }
  if (_dup2(from, to) < 0) {
    return -1;
  }
  if ((unsigned)from < 65536u && (unsigned)to < 65536u) {
    win_file_flags[to] = win_file_flags[from];
  }
  return to;
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

static int win_getsockname(int fd, struct sockaddr* a, int* n) {
  if (getsockname(win_sok(fd), a, n) == 0) {
    return 0;
  }
  errno = win_wsa_errno();
  return -1;
}

static int socketpair(int domain, int type, int proto, int sv[2]) {
  (void)domain;
  (void)type;
  (void)proto;
  win_init();
  SOCKET li = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (li == INVALID_SOCKET || bind(li, (struct sockaddr*)&a, sizeof a) != 0
    || listen(li, 1) != 0) {
    if (li != INVALID_SOCKET) closesocket(li);
    errno = EIO;
    return -1;
  }
  int alen = sizeof a;
  getsockname(li, (struct sockaddr*)&a, &alen);
  SOCKET c = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
  if (c == INVALID_SOCKET || connect(c, (struct sockaddr*)&a, sizeof a) != 0) {
    closesocket(li);
    if (c != INVALID_SOCKET) closesocket(c);
    errno = EIO;
    return -1;
  }
  SOCKET s = accept(li, NULL, NULL);
  closesocket(li);
  if (s == INVALID_SOCKET) {
    closesocket(c);
    errno = EIO;
    return -1;
  }
  sv[0] = win_sockfd(s);
  sv[1] = win_sockfd(c);
  return sv[0] < 0 || sv[1] < 0 ? -1 : 0;
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
  if (win_on_connect != NULL) {
    int out = 0;
    if (win_on_connect(fd, a, n, &out)) {
      return out;
    }
  }
  if (connect(win_sok(fd), a, n) == 0) {
    return 0;
  }
  int e = WSAGetLastError();
  errno = e == WSAEWOULDBLOCK ? EINPROGRESS : win_wsa_errno();
  return -1;
}

static int win_accept(int fd, struct sockaddr* a, int* n) {
  if (win_on_accept != NULL) {
    int out = 0;
    if (win_on_accept(fd, a, n, &out)) {
      return out;
    }
  }
  SOCKET s = accept(win_sok(fd), a, n);
  if (s == INVALID_SOCKET) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return win_sockfd(s);
}

static ssize_t win_send(int fd, const void* buf, size_t n, int flags) {
  if (win_on_send != NULL) {
    ssize_t out = 0;
    if (win_on_send(fd, buf, n, flags, &out)) {
      return out;
    }
  }
  int r = send(win_sok(fd), (const char*)buf, n > INT_MAX ? INT_MAX : (int)n, flags);
  if (r < 0) {
    int e = WSAGetLastError();
    errno = e == WSAEWOULDBLOCK ? EAGAIN : win_wsa_errno();
    return -1;
  }
  return r;
}

static ssize_t win_recv(int fd, void* buf, size_t n, int flags) {
  if (win_on_recv != NULL) {
    ssize_t out = 0;
    if (win_on_recv(fd, buf, n, flags, &out)) {
      return out;
    }
  }
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
  if (win_on_sendto != NULL) {
    ssize_t out = 0;
    if (win_on_sendto(fd, buf, n, flags, a, alen, &out)) {
      return out;
    }
  }
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
  if (win_on_recvfrom != NULL) {
    ssize_t out = 0;
    if (win_on_recvfrom(fd, buf, n, flags, a, alen, &out)) {
      return out;
    }
  }
  int r = recvfrom(win_sok(fd), (char*)buf, n > INT_MAX ? INT_MAX : (int)n,
    flags, a, alen);
  if (r < 0) {
    int e = WSAGetLastError();
    if (e == WSAEMSGSIZE) {
      return n > INT_MAX ? INT_MAX : (int)n;
    }
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
    unsigned f = fd >= 0 && (unsigned)fd < 65536u ? win_file_flags[fd] : 0;
    return (int)(f & ~WIN_FIFO_BIT);
  }
  if (cmd == F_SETFL) {
    if (fd >= 0 && (unsigned)fd < 65536u) {
      unsigned keep = win_file_flags[fd] & WIN_FIFO_BIT;
      win_file_flags[fd] = (unsigned)arg | keep;
      if (keep) {
        DWORD mode = (arg & O_NONBLOCK) ? PIPE_NOWAIT : PIPE_WAIT;
        SetNamedPipeHandleState((HANDLE)_get_osfhandle(fd), &mode, NULL, NULL);
        return 0;
      }
    }
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_DISK) {
      return 0;
    }
    u_long nb = (arg & O_NONBLOCK) ? 1 : 0;
    if (ioctlsocket((SOCKET)h, FIONBIO, &nb) == 0) {
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
#define getsockname win_getsockname
#define dup2       win_dup2
#define accept     win_accept
#define send       win_send
#define recv       win_recv
#define sendto     win_sendto
#define recvfrom   win_recvfrom
#define setsockopt win_setsockopt
#define getsockopt win_getsockopt
#define select     win_select
#define read       win_read
#define write      win_write
#ifdef off_t
#undef off_t
#endif
#define off_t long long

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

#define fstat _fstat64
#define stat  _stat64

static ssize_t pwrite(int fd, const void* buf, size_t n, off_t off) {
  if (win_on_pwrite != NULL) {
    ssize_t out = 0;
    if (win_on_pwrite(fd, buf, n, (long long)off, &out)) {
      return out;
    }
  }
  __int64 cur = _telli64(fd);
  if (cur < 0 || _lseeki64(fd, off, SEEK_SET) < 0) {
    return -1;
  }
  int r = _write(fd, buf, n > INT_MAX ? INT_MAX : (unsigned)n);
  _lseeki64(fd, cur, SEEK_SET);
  return r;
}

static pthread_t pthread_self(void) {
  return (pthread_t)(uintptr_t)GetCurrentThreadId();
}

static int sched_yield(void) {
  if (!SwitchToThread()) {
    Sleep(0);
  }
  return 0;
}

static int ftrylockfile(FILE* f) {
  (void)f;
  return 0;
}

#ifdef FAR
#undef FAR
#endif

#if !defined(__MINGW32__)
#define strdup _strdup
#endif

static const char* win_strerror(int e) {
  switch (e) {
    case 1: return "Operation not permitted";
    case 2: return "No such file or directory";
    case 5: return "Input/output error";
    case 9: return "Bad file descriptor";
    case 11: return "Resource temporarily unavailable";
    case 13: return "Permission denied";
    case 17: return "File exists";
    case 20: return "Not a directory";
    case 21: return "Is a directory";
    case 22: return "Invalid argument";
    case 27: return "File too large";
    case 28: return "No space left on device";
    case 32: return "Broken pipe";
    case 98: return "Address already in use";
    case 110: return "Connection timed out";
    case 111: return "Connection refused";
    default: return "Unknown error";
  }
}

static void cuda_load(void) {
  const char* home = getenv("CUDA_PATH");
  if (home == NULL) {
    home = getenv("CUDA_HOME");
  }
  if (home == NULL) {
    return;
  }
  char pat[MAX_PATH];
  snprintf(pat, sizeof pat, "%s\\bin\\x64\\nvrtc64_*.dll", home);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    return;
  }
  char best[MAX_PATH];
  best[0] = 0;
  do {
    if (strcmp(fd.cFileName, best) > 0) {
      snprintf(best, sizeof best, "%s", fd.cFileName);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  char full[MAX_PATH];
  snprintf(full, sizeof full, "%s\\bin\\x64\\%s", home, best);
  LoadLibraryA(full);
}

static void __attribute__((constructor)) cuda_load_use(void) {
  cuda_load();
}

#endif
