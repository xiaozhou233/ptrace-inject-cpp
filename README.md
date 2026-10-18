# ptrace_inject

Inject a shared library (`.so`) into a running x86-64 Linux process with
`ptrace`.

> Chinese version: [README.ZH.md](README.ZH.md)

This repository provides:

- **A C ABI library** `libptrace_inject` (implemented in C++, but the public
  header is directly usable from C and C++);
- **A thin command line tool** `ptrace-inject`, plus a few example programs.

The core flow is: attach to the target threads → remotely call
`malloc` / `dlopen` / `free` inside the target → restore the original state →
detach. There is no spawn mode and no ELF parsing.

## Files

```
include/ptrace_inject.h   # public C API
src/
├── library.cpp           # C ABI implementation, attach/inject/detach flow
├── log.cpp/.h            # internal logging callback
├── inject.cpp/.h         # patch-site/stack selection, remote calls
├── ptrace_utils.cpp/.h   # attach/detach, registers, wait_for_trap
├── symbols.cpp/.h        # locate and relocate malloc/dlopen/free
├── maps.cpp/.h           # /proc/<pid>/maps parsing, address checks
├── memory.cpp/.h         # /proc/<pid>/mem read/write
├── process_lookup.cpp/.h # find a PID by executable name
├── util.cpp/.h           # error and string helpers
├── config.h              # shellcode, stack margin, etc.
└── main.cpp              # CLI
examples/
├── inject_by_pid.c       # C usage example
├── inject_by_name.cpp    # C++ usage example
└── sample_library.c      # sample library to inject
CMakeLists.txt
```

## Building

```bash
cd /home/xiaozhou/Development/inject

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This builds a static library by default. To build a shared library instead:

```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared -j
```

Optional switches:

```bash
-DPTRACE_INJECT_BUILD_CLI=OFF       # do not build ptrace-inject
-DPTRACE_INJECT_BUILD_EXAMPLES=OFF  # do not build the examples
```

Build artifacts:

```
build/libptrace_inject.a          # static library (default)
build-shared/libptrace_inject.so  # shared library (with BUILD_SHARED_LIBS=ON)
build/ptrace-inject               # CLI
build/example_by_pid              # C example
build/example_by_name             # C++ example
build/libptrace_inject_sample.so
```

There are intentionally **no install rules**; point your build at `include/`
and `build/` directly (see below).

## Using it as a library

The public header is C-compatible. The core API is just two functions:

```c
int ptrace_inject_by_pid(pid_t pid,
                         const char *library_path,
                         const ptrace_inject_options *options,
                         char *error,
                         size_t error_size);

int ptrace_inject_by_name(const char *process_name,
                          const char *library_path,
                          const ptrace_inject_options *options,
                          pid_t *resolved_pid,
                          char *error,
                          size_t error_size);
```

- Returns `PTRACE_INJECT_OK` (0) or `PTRACE_INJECT_ERROR` (-1).
- `options` may be `NULL` (silent); `options.log_fn` receives debug/progress
  messages.
- `error` may be `NULL`; when non-NULL, it receives a NUL-terminated error
  message on failure.
- `resolved_pid` in `ptrace_inject_by_name` may be `NULL`.

Minimal example:

```c
#include <stdio.h>
#include "ptrace_inject.h"

static void on_log(int level, const char *message, void *user_data) {
  (void)level;
  (void)user_data;
  fprintf(stderr, "%s\n", message);
}

int main(void) {
  ptrace_inject_options options = {0};
  options.log_fn = on_log;

  char error[512] = {0};
  if (ptrace_inject_by_pid(1234, "/abs/path/libfoo.so",
                           &options, error, sizeof(error)) != PTRACE_INJECT_OK) {
    fprintf(stderr, "inject failed: %s\n", error);
    return 1;
  }
  return 0;
}
```

### Linking against the build tree

No installation is required; point the compiler at this repository's
`include/` and `build/` directories.

C++ program:

```bash
g++ app.cpp -I/path/to/inject/include \
    /path/to/inject/build/libptrace_inject.a -ldl -o app
```

C program (the static library is C++ internally, so linking with `g++` is the
easiest path):

```bash
gcc -c app.c -I/path/to/inject/include -o app.o
g++ app.o /path/to/inject/build/libptrace_inject.a -ldl -o app
```

Shared library:

```bash
gcc app.c -I/path/to/inject/include \
    -L/path/to/inject/build-shared -lptrace_inject -ldl -lstdc++ -o app
LD_LIBRARY_PATH=/path/to/inject/build-shared ./app
```

If your project also uses CMake, you can use `add_subdirectory`; no install
step is needed:

```cmake
# Your project() must enable CXX (the library is C++; this matters with
# add_subdirectory).
project(consumer LANGUAGES C CXX)

set(PTRACE_INJECT_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(PTRACE_INJECT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(/path/to/inject ptrace_inject)

add_executable(app app.c)
target_link_libraries(app PRIVATE ptrace_inject)
```

## Command line tool

```bash
# By PID
./build/ptrace-inject -p <PID> /absolute/or/relative/path/to/library.so

# By executable name
./build/ptrace-inject -n target-process ./library.so
```

Example output:

```
[*] attached to 4 thread(s) of pid 12345
[debug] remote malloc: 0x..., dlopen: 0x..., free: 0x...
[debug] patched shellcode at 0x55...
[debug] remote malloc(38) = 0x55...
[debug] remote dlopen("./libhello.so") = 0x55...
[debug] remote free() done
[*] restored original code and registers
[*] injected ./libhello.so into pid 12345
```

## Example programs

The repository ships three examples:

```bash
# 1. Sample library to inject: its constructor prints and writes
#    /tmp/ptrace_inject_sample.txt.
./build/libptrace_inject_sample.so

# 2. Inject by PID (C example)
./build/example_by_pid <PID> ./build/libptrace_inject_sample.so

# 3. Inject by process name (C++ example)
./build/example_by_name <process-name> ./build/libptrace_inject_sample.so
```

`examples/inject_by_pid.c` shows the minimal C call;
`examples/inject_by_name.cpp` shows the C++ call, the log callback, and the
`resolved_pid` output parameter.

## How it works

### 1. Stop every thread

`attach_all_threads()` walks `/proc/<pid>/task` and calls `PTRACE_ATTACH` +
`waitpid` for each thread. The target may create new threads while we are
attaching, so it re-scans and stops those too (up to `kMaxAttachRounds`
rounds).

### 2. Choose a place and write the shellcode

Everything is validated first (see "Safety measures"). Then it picks a
private, file-backed, executable mapping from `/proc/<pid>/maps`, chooses a
page-aligned address in it that no stopped thread is currently executing at,
saves the original 6 bytes, and writes the shellcode via `pwrite` on
`/proc/<pid>/mem`:

```asm
90              nop
90              nop
41 ff d1        call r9
cc              int3
```

### 3. Compute the address of the functions in the target

`dlsym(RTLD_DEFAULT, ...)` gives the local addresses of `malloc`, `dlopen`,
and `free`. `dladdr` tells us which shared object owns each symbol (on new
glibc `dlopen` is in `libc.so.6`; on old glibc it may be in `libdl.so.2`).
The target address is computed from the object's base in both processes:

```text
target address = local symbol address
               - local object base
               + target object base
```

After relocation it verifies that both sides use the same `device:inode` for
that object and that the resulting address is executable in the target.
Otherwise it refuses to continue.

### 4. Remote calls

`call_in_tracee()` does this:

```text
PTRACE_GETREGS        save the original registers (once)
set rip = shellcode + 2
    r9  = target function
    rdi = argument 1
    rsi = argument 2
    rsp = (original rsp - 256), 16-byte aligned
PTRACE_SETREGS
PTRACE_CONT
waitpid until SIGTRAP, and RIP must be exactly shellcode + 6
PTRACE_GETREGS, read rax as the return value
```

`rip = shellcode + 2` skips the two `nop`s. If the kernel rewinds RIP by 2
when resuming the thread, execution starts at the `nop`s instead, and the nop
slide still reaches `call r9`.

### 5. Load the `.so`

1. Canonicalize the path and append `\0`;
2. remotely call `malloc(path_len)`;
3. write the path into that target memory;
4. remotely call `dlopen(path_addr, RTLD_LAZY)`;
5. remotely call `free(path_addr)`.

### 6. Restore

Write the original 6 bytes back, restore all registers, and `PTRACE_DETACH`
every thread.

## Safety measures

- **Validate before modifying**: library path, libc build consistency, remote
  function addresses, patch site, and stack writability are all checked before
  the shellcode is written. On failure the target has not been modified at all.
- **Refuse dangerous targets**: `pid <= 1` (init) and the injector itself are
  rejected.
- **Keep the patch site away from executing code**: only private,
  file-backed, executable mappings are considered, and any page containing a
  stopped thread's current RIP is skipped.
- **Do not clobber the red zone**: remote calls use `(rsp - 256) & ~0xf` and
  verify that the address is in a writable mapping.
- **Only accept our own SIGTRAP**: when waiting for `int3`, it also checks
  `RIP == shellcode + 6`.
- **Do not swallow signals**: if an unexpected signal arrives while the
  shellcode runs, the code/registers are restored and the signal is delivered
  back to the corresponding thread via `PTRACE_DETACH`.
- **Best-effort recovery**: any failed remote call attempts to write the
  original bytes and registers back.
- **The library is silent by default**: logs are emitted only when the caller
  provides `log_fn`; the shared library exports only
  `ptrace_inject_by_pid` and `ptrace_inject_by_name`.

## Testing

With `kernel.yama.ptrace_scope=1`, attaching to a non-child process is
rejected. A test victim can opt in with:

```c
prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
```

Then:

```bash
./victim &
./build/example_by_pid $! ./build/libptrace_inject_sample.so
cat /tmp/ptrace_inject_sample.txt
```

If the library's constructor writes to the file, the library really ran inside
the target process.

## Limitations

- **x86-64 Linux only.**
- **Requires ptrace permission**: same uid plus `CAP_SYS_PTRACE`, or adjust
  `/proc/sys/kernel/yama/ptrace_scope`, or have the target call
  `PR_SET_PTRACER`.
- **Both sides must use the same libc**: inconsistent `libc.so.6` /
  `libdl.so.2` builds are detected and rejected via `device:inode`, but the
  two processes still need the same build.
- **Possible deadlock**: all other threads are stopped during injection. If
  one of them holds the `malloc` or dynamic-linker lock, the remote `dlopen`
  can hang.
- **It still temporarily modifies code**: the patch site avoids current RIPs
  and is restored immediately, but the target's code segment is modified.
- **Signal forwarding is best-effort**: an unexpected signal is delivered on
  detach, but the interrupted libc call has already been abandoned and may
  have left partial state.
- **No `dlclose`**: the loaded library stays in the target process, which is
  the expected behaviour for injection.
- **Path visibility**: `canonicalize` runs in the injector process, so the
  target must be able to access the same path (same mount namespace).

## Acknowledgements

This project is a C++ port of
[Artemis21/ptrace-inject](https://github.com/Artemis21/ptrace-inject).
The original idea, the shellcode-based injection approach, and much of the
design come from that project.
