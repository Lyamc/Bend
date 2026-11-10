#include <sys/stat.h>

#ifndef _WIN32
// The peer: echo the first line to the real stdout, write "hi" into the
// FIFO ($1) and remove it, then copy the rest through. A run that dies
// first must not leave it holding stdout: no line, it exits; a FIFO still
// unread after 10 s, a watchdog removes it and kills the peer.
#define PEER_SH "IFS= read -r l || { rm -f \"$1\"; exit 1; };" \
  " printf '%s\\n' \"$l\"; (sleep 10; [ -p \"$1\" ] && rm -f \"$1\"" \
  " && kill $$) >/dev/null 2>&1 & printf hi > \"$1\"; rm -f \"$1\"; exec cat"
#endif

#ifdef _WIN32
struct peer_job {
  int in;
  int out;
  char path[64];
};

static DWORD WINAPI peer_thread(void* raw) {
  struct peer_job* j = (struct peer_job*)raw;
  char line[256];
  int n = 0;
  while (n < (int)sizeof line - 1) {
    char c;
    int r = _read(j->in, &c, 1);
    if (r <= 0) {
      break;
    }
    line[n++] = c;
    if (c == '\n') {
      break;
    }
  }
  if (n > 0) {
    _write(j->out, line, (unsigned)n);
  }
  int wr = -1;
  for (int i = 0; i < 2000 && wr < 0; i += 1) {
    wr = open(j->path, O_WRONLY);
    if (wr < 0) {
      Sleep(5);
    }
  }
  if (wr >= 0) {
    _write(wr, "hi\n", 3);
    close(wr);
  }
  return 0;
}
#endif

static int peer_out = -1;

// Moves fd 1 onto a pipe the peer reads, and answers the FIFO's path.
Term peer_arm_run(Env e, Term* f, IoWork* w) {
  char path[64];
  int  p[2];
  snprintf(path, sizeof path, "/tmp/bend_peer_%d", (int)getpid());
  unlink(path);
#ifdef _WIN32
  if (mkfifo(path, 0600) != 0 || pipe(p) != 0) {
    err_fail("peer_arm: the FIFO, pipe or peer failed");
  }
  struct peer_job* j = (struct peer_job*)malloc(sizeof *j);
  j->in = p[0];
  j->out = dup(1);
  snprintf(j->path, sizeof j->path, "%s", path);
  CreateThread(NULL, 0, peer_thread, j, 0, NULL);
  peer_out = j->out;
  dup2(p[1], 1);
  close(p[1]);
  setvbuf(stdout, NULL, _IONBF, 0);
#else
  pid_t child = -1;
  if (mkfifo(path, 0600) != 0 || pipe(p) != 0 || (child = fork()) < 0) {
    err_fail("peer_arm: the FIFO, pipe or peer failed");
  }
  if (child == 0) {
    dup2(p[0], 0);
    close(p[0]);
    close(p[1]);
    execl("/bin/sh", "sh", "-c", PEER_SH, "sh", path, (char*)NULL);
    _exit(127);
  }
  peer_out = dup(1);
  dup2(p[1], 1);
  close(p[0]);
  close(p[1]);
#endif
  return io_str(e, path, strlen(path));
}

// Puts fd 1 back; the peer's cat sees the pipe close and exits.
Term peer_done_run(Env e, Term* f, IoWork* w) {
  dup2(peer_out, 1);
  close(peer_out);
  return term_pak(CID(Unit), 0);
}

static void __attribute__((constructor)) peer_use(void) {
  io_eff(CID(peer_arm), peer_arm_run);
  io_eff(CID(peer_done), peer_done_run);
}
