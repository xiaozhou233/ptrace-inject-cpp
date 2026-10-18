/*
 * A sample shared library to inject.  Its constructor runs inside the target
 * process as soon as dlopen() loads it.
 *
 * Build (done automatically by the CMake examples target):
 *   cc -shared -fPIC -o ptrace_inject_sample.so examples/sample_library.c
 */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define SAMPLE_OUTPUT_FILE "/tmp/ptrace_inject_sample.txt"

__attribute__((constructor)) static void on_load(void) {
  const char* message = "sample_library: constructor ran\n";

  (void)!write(STDOUT_FILENO, message, strlen(message));

  const int fd =
      open(SAMPLE_OUTPUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    (void)!write(fd, message, strlen(message));
    close(fd);
  }
}
