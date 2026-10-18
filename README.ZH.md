# ptrace-inject-cpp

> English version: [README.md](README.md)

一个用 `ptrace` 向 x86-64 Linux 进程注入共享库的 C++ 实现。

本项目是 [Artemis21/ptrace-inject](https://github.com/Artemis21/ptrace-inject)
的 C++ 移植版。

项目名是 `ptrace-inject-cpp`；生成的库、头文件和公开 API 仍然叫 `ptrace_inject`。

提供两样东西：

- **一个 C ABI 库** `libptrace_inject`（实现是 C++，头文件可直接给 C/C++ 用）；
- **一个薄命令行工具** `ptrace-inject-cpp`，以及几个示例程序。

核心流程：attach 目标线程 → 在目标里远程调用
`malloc` / `dlopen` / `free` → 恢复现场 → detach。没有 spawn 模式，也没有
ELF 解析。

## 文件

```
include/ptrace_inject.h   # 公开 C API
src/
├── library.cpp           # C ABI 实现、attach/inject/detach 编排
├── log.cpp/.h            # 内部日志回调
├── inject.cpp/.h         # 选补丁点、选栈、远程调用、注入流程
├── ptrace_utils.cpp/.h   # attach/detach、寄存器、wait_for_trap
├── symbols.cpp/.h        # malloc/dlopen/free 地址定位与换算
├── maps.cpp/.h           # /proc/<pid>/maps 解析、地址检查
├── memory.cpp/.h         # /proc/<pid>/mem 读写
├── process_lookup.cpp/.h # 按名字找 PID
├── util.cpp/.h           # 错误、字符串工具
├── config.h              # shellcode、栈余量等常量
└── main.cpp              # CLI
examples/
├── inject_by_pid.c       # C 调用示例
├── inject_by_name.cpp    # C++ 调用示例
└── sample_library.c      # 被注入的示例 .so
CMakeLists.txt
```

## 构建

```bash
cd /path/to/ptrace-inject-cpp

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

默认生成静态库。要生成共享库：

```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared -j
```

可选开关：

```bash
-DPTRACE_INJECT_BUILD_CLI=OFF       # 不构建 ptrace-inject-cpp
-DPTRACE_INJECT_BUILD_EXAMPLES=OFF  # 不构建示例
```

构建产物：

```
build/libptrace_inject.a          # 静态库（默认）
build-shared/libptrace_inject.so  # 共享库（BUILD_SHARED_LIBS=ON 时的目录）
build/ptrace-inject-cpp               # CLI
build/example_by_pid              # C 示例
build/example_by_name             # C++ 示例
build/libptrace_inject_sample.so
```

## 作为库使用

公开头文件是 C 兼容的，核心接口只有两个：

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

- 返回 `PTRACE_INJECT_OK`（0）或 `PTRACE_INJECT_ERROR`（-1）。
- `options` 可以为 `NULL`（静默）；`options.log_fn` 可接收调试/进度日志。
- `error` 可以为 `NULL`；非空时失败会写入 NUL 结尾的错误信息。
- `ptrace_inject_by_name` 的 `resolved_pid` 可以为 `NULL`。

最小调用示例：

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

### 链接构建产物

不做安装，直接指向本仓库的 `include/` 和 `build/` 即可。

C++ 程序：

```bash
g++ app.cpp -I/path/to/ptrace-inject-cpp/include \
    /path/to/ptrace-inject-cpp/build/libptrace_inject.a -ldl -o app
```

C 程序（静态库内部是 C++，用 g++ 链接最省事）：

```bash
gcc -c app.c -I/path/to/ptrace-inject-cpp/include -o app.o
g++ app.o /path/to/ptrace-inject-cpp/build/libptrace_inject.a -ldl -o app
```

共享库：

```bash
gcc app.c -I/path/to/ptrace-inject-cpp/include \
    -L/path/to/ptrace-inject-cpp/build-shared -lptrace_inject -ldl -lstdc++ -o app
LD_LIBRARY_PATH=/path/to/ptrace-inject-cpp/build-shared ./app
```

如果自己的项目也用 CMake，可以直接 `add_subdirectory`，不需要安装：

```cmake
# 你的 project() 里要启用 CXX（库本身是 C++；这点在 add_subdirectory 时很关键）
project(consumer LANGUAGES C CXX)

set(PTRACE_INJECT_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(PTRACE_INJECT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(/path/to/ptrace-inject-cpp ptrace_inject)

add_executable(app app.c)
target_link_libraries(app PRIVATE ptrace_inject)
```

## 命令行工具

```bash
# 按 PID
./build/ptrace-inject-cpp -p <PID> /absolute/or/relative/path/to/library.so

# 按可执行文件名
./build/ptrace-inject-cpp -n target-process ./library.so
```

示例输出：

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

## 示例程序

仓库里带了三个示例：

```bash
# 1. 示例被注入库：constructor 会打印并写 /tmp/ptrace_inject_sample.txt
./build/libptrace_inject_sample.so

# 2. 按 PID 注入（C 示例）
./build/example_by_pid <PID> ./build/libptrace_inject_sample.so

# 3. 按进程名注入（C++ 示例）
./build/example_by_name <process-name> ./build/libptrace_inject_sample.so
```

`examples/inject_by_pid.c` 展示了最小 C 调用；`examples/inject_by_name.cpp`
展示了 C++ 调用、日志回调以及 `resolved_pid` 的用法。

## 原理

### 1. 停住所有线程

`attach_all_threads()` 枚举 `/proc/<pid>/task`，对每个线程 `PTRACE_ATTACH` +
`waitpid`。目标可能在我们 attach 期间新建线程，所以之后会重新枚举，把新线程
也停住（最多 `kMaxAttachRounds` 轮）。

### 2. 找位置并写入 shellcode

在真正改写前先做完整校验（见「安全措施」），然后从 `/proc/<pid>/maps` 选一个
private、file-backed、可执行的映射，按页对齐挑一个没有已停线程 RIP 落在其中的
地址，保存原始 6 字节后通过 `/proc/<pid>/mem` 的 `pwrite` 写入：

```asm
90              nop
90              nop
41 ff d1        call r9
cc              int3
```

### 3. 计算目标进程里的函数地址

用 `dlsym(RTLD_DEFAULT, ...)` 拿到注入器进程里 `malloc`、`dlopen`、`free`
的地址，再用 `dladdr` 确认每个符号属于哪个共享对象（新 glibc 里 `dlopen`
在 `libc.so.6`，老 glibc 里可能在 `libdl.so.2`），然后按两边该对象的基址换算：

```text
目标地址 = 本进程符号地址 - 该对象在本进程的基址 + 该对象在目标进程的基址
```

换算后会校验：两边该对象的 `device:inode` 必须相同，且目标地址必须落在可执行
映射内；否则直接拒绝。

### 4. 远程调用

`call_in_tracee()`：

```text
PTRACE_GETREGS        保存原始寄存器（只做一次）
设置 rip = shellcode + 2
     r9  = 目标函数
     rdi = 参数 1
     rsi = 参数 2
     rsp = (原始 rsp - 256) 按 16 字节对齐
PTRACE_SETREGS
PTRACE_CONT
waitpid 等到 SIGTRAP，且 RIP 必须正好是 shellcode+6
PTRACE_GETREGS 读 rax 作为返回值
```

`rip = shellcode + 2` 跳过两个 `nop`。如果内核恢复线程时把 RIP 回退 2 字节，
就会落到 `nop` 上，nop slide 仍然能走到 `call r9`。

### 5. 加载 `.so`

1. `canonicalize` 路径并补 `\0`；
2. 远程 `malloc(path_len)`；
3. 把路径写进目标进程那块内存；
4. 远程 `dlopen(path_addr, RTLD_LAZY)`；
5. 远程 `free(path_addr)`。

### 6. 恢复

把原始 6 字节写回，恢复完整寄存器，`PTRACE_DETACH` 所有线程。

## 安全措施

- **先校验，后改写**：库路径、libc 构建一致性、远程函数地址、补丁点、栈可写性
  全部在写入 shellcode 之前完成。失败时目标完全没有被修改。
- **拒绝危险目标**：`pid <= 1`（init）以及注入器自身直接拒绝。
- **补丁点避开执行流**：只选 private + file-backed + 可执行映射，并跳过任何
  已停线程当前 RIP 所在的那一页。
- **栈不踩红区**：远程调用使用 `(rsp - 256) & ~0xf`，并确认落在可写映射中。
- **只认自己的 SIGTRAP**：等待 `int3` 时同时校验 `RIP == shellcode + 6`。
- **不吞信号**：shellcode 执行期间收到意外信号时，恢复代码/寄存器后通过
  `PTRACE_DETACH` 把信号回传给对应线程。
- **出错也尽量恢复**：任何远程调用失败都会尝试写回原始字节和寄存器。
- **库默认静默**：只有调用方传了 `log_fn` 才会输出日志；共享库只导出
  `ptrace_inject_by_pid` / `ptrace_inject_by_name` 两个符号。

## 测试

本机 `kernel.yama.ptrace_scope=1` 时，非父子进程 attach 会被拒绝。测试用的
victim 可以自己调用：

```c
prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
```

然后：

```bash
./victim &
./build/example_by_pid $! ./build/libptrace_inject_sample.so
cat /tmp/ptrace_inject_sample.txt
```

如果 lib 的 constructor 往文件里写内容，就能确认它确实在目标进程里执行了。

## 局限

- **只支持 x86-64 Linux**。
- **需要 ptrace 权限**：同 uid + `CAP_SYS_PTRACE`，或调整
  `/proc/sys/kernel/yama/ptrace_scope`，或目标自己调用 `PR_SET_PTRACER`。
- **两边 libc 必须一致**：会用 `device:inode` 检测并拒绝不一致的
  `libc.so.6`/`libdl.so.2`，但仍要求两边是同一份构建。
- **可能死锁**：注入时其他线程全部停住。如果某个线程正好持有
  `malloc`/动态链接器的锁，目标里的 `dlopen` 会卡住。
- **仍然是临时改写代码**：补丁点避开了当前 RIP 并会立即恢复，但本质上还是
  改动目标代码段。
- **信号转发是尽力而为**：意外信号会在 detach 时回传，但此时正在执行的
  libc 调用已被放弃，可能有半完成的状态。
- **不 `dlclose`**：加载后的库会一直留在目标进程里，符合「注入」的预期。
- **路径可见性**：`canonicalize` 发生在注入器进程，目标进程必须能访问同一路径
  （同一 mount namespace）。

## 致谢

本项目是 [Artemis21/ptrace-inject](https://github.com/Artemis21/ptrace-inject)
的 C++ 移植版。整体思路、基于 shellcode 的注入方式以及很多设计细节都来自原项目。
