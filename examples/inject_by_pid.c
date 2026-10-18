/*
 * Minimal C example: inject a shared library into a PID.
 *
 * Build against the installed library:
 *   cc inject_by_pid.c -lptrace_inject -o inject_by_pid
 *
 * Or against the build tree:
 *   cc inject_by_pid.c -I../include ../build/libptrace_inject.a -ldl -lstdc++
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ptrace_inject.h"

static void on_log(int level, const char* message, void* user_data) {
  (void)user_data;
  const char* prefix = level >= PTRACE_INJECT_LOG_WARN ? "[!]" : "[*]";
  fprintf(stderr, "%s %s\n", prefix, message);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <pid> <library.so>\n", argv[0]);
    return 1;
  }

  ptrace_inject_options options;
  memset(&options, 0, sizeof(options));
  options.log_fn = on_log;

  const long pid = strtol(argv[1], NULL, 10);
  char error[512] = {0};

  const int result =
      ptrace_inject_by_pid((pid_t)pid, argv[2], &options, error, sizeof(error));
  if (result != PTRACE_INJECT_OK) {
    fprintf(stderr, "inject failed: %s\n",
            error[0] != '\0' ? error : "(no error message)");
    return 1;
  }
  return 0;
}
