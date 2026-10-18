// C++ example: inject a shared library into a process found by name and print
// the resolved PID.

#include <cstdio>

#include "ptrace_inject.h"

static void on_log(int level, const char* message, void*) {
  const char* prefix = "[*]";
  switch (level) {
    case PTRACE_INJECT_LOG_DEBUG:
      prefix = "[debug]";
      break;
    case PTRACE_INJECT_LOG_WARN:
      prefix = "[!]";
      break;
    case PTRACE_INJECT_LOG_ERROR:
      prefix = "[error]";
      break;
    default:
      break;
  }
  std::fprintf(stderr, "%s %s\n", prefix, message);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <process-name> <library.so>\n", argv[0]);
    return 1;
  }

  ptrace_inject_options options{};
  options.log_fn = on_log;

  pid_t resolved_pid = -1;
  char error[512] = {};

  const int result = ptrace_inject_by_name(
      argv[1], argv[2], &options, &resolved_pid, error, sizeof(error));
  if (result != PTRACE_INJECT_OK) {
    std::fprintf(stderr, "inject failed: %s\n", error);
    return 1;
  }

  std::printf("injected into pid %d\n", static_cast<int>(resolved_pid));
  return 0;
}
