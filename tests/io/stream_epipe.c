#ifndef _WIN32
#include <sys/wait.h>
#endif

// A forked child moves fd onto a pipe whose read end is closed and
// writes a line there with io_out and io_sync, as IO.print does. The
// answer is the child's status as a shell shows it: 128 plus the signal
// that ended it, else its exit code. With resume set, the child returns
// into Bend instead, to print and exit through the real IO loop.
// Windows has no fork, so the write runs in a child of this executable
// that inherits the pipe as that standard stream and exits 141 on EPIPE.
Term epipe_write_run(Env e, Term* f, IoWork* w) {
  int fd = (int)(u32)f[0];
  io_sync();
#ifdef _WIN32
  (void)e;
  (void)w;
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof sa);
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  HANDLE rd = NULL;
  HANDLE wr = NULL;
  if (!CreatePipe(&rd, &wr, &sa, 0)) {
    err_fail("epipe pipe");
  }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = fd == 1 ? wr : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = fd == 2 ? wr : GetStdHandle(STD_ERROR_HANDLE);
  SetEnvironmentVariableA("BEND_EPIPE_CHILD", fd == 2 ? "2" : "1");
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof pi);
  char cmd[1024];
  snprintf(cmd, sizeof cmd, "%s", GetCommandLineA());
  BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
  SetEnvironmentVariableA("BEND_EPIPE_CHILD", NULL);
  CloseHandle(wr);
  CloseHandle(rd);
  if (!ok) {
    err_fail("epipe child");
  }
  WaitForSingleObject(pi.hProcess, 10000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (u32)code;
#else
  int p[2];
  pipe(p);
  close(p[0]);
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], fd);
    if (f[1] != 0) return 0;
    io_out(fd == 1 ? stdout : stderr, "x\n", 2);
    io_sync();
    _exit(0);
  }
  close(p[1]);
  int s = 0;
  waitpid(pid, &s, 0);
  return (u32)(WIFSIGNALED(s) ? 128 + WTERMSIG(s) : WEXITSTATUS(s));
#endif
}

static void __attribute__((constructor)) epipe_write_use(void) {
  io_eff(CID(Epipe.write), epipe_write_run);
}
