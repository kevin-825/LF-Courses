# LFD401 Chapter 6: System Calls — Deep-Dive Study Guide & Lab Manual

This study guide covers the **System Calls** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), explaining the boundary between `glibc` library functions and Linux kernel system calls ([`syscalls(2)`](https://man7.org/linux/man-pages/man2/syscalls.2.html)), the hardware trap mechanism (`syscall` / `ecall`), indirect invocation via [`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html), and thread-safe error handling via [`errno(3)`](https://man7.org/linux/man-pages/man3/errno.3.html), followed by **Milestone 6** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. System Calls vs. Library Functions

Every Linux process runs in **User Space** (unprivileged CPU Ring 3 on `x86_64` / U-mode on `riscv64`) with isolated virtual memory. Only the **Linux Kernel** runs in **Kernel Space** (Ring 0 / S-mode) with direct access to hardware, page tables, the VFS, and process scheduling.

| Dimension | System Call (`man 2`) | C Library Function (`man 3`) |
| :--- | :--- | :--- |
| **Execution Privilege** | Transitions CPU from User Space → Kernel Space and back. | Executes entirely in User Space (unless it internally invokes a syscall). |
| **Examples** | `openat()`, `read()`, `write()`, `fork()`, `clone()`, `mmap()`, `getpid()` | `strlen()`, `qsort()`, `memcpy()`, `printf()`, `fopen()`, `malloc()` |
| **Cost & Buffering** | Higher overhead (CPU privilege switch, register save/restore, Spectre/Meltdown mitigations, TLB/cache impact). Unbuffered. | Fast function call; `stdio` (`FILE *`) buffers bytes in user space and batches them into fewer `write()` syscalls. |
| **Tracing Tool** | [`strace(1)`](https://man7.org/linux/man-pages/man1/strace.1.html) (intercepts kernel boundary via `ptrace`) | `ltrace(1)` (intercepts dynamic library PLT calls) |

### Why Most "System Calls" You Call in C Are Actually `glibc` Wrappers
When your C code calls `write(fd, buf, len)`, you are **not** jumping straight into the kernel—you are calling a tiny `glibc` wrapper function in `libc.so.6` that:
1. Moves your C arguments into the architecture's syscall registers.
2. Places the kernel system call number (`__NR_write`) into the syscall-number register.
3. Executes the hardware trap instruction (`syscall` on `x86_64`, `ecall` on `riscv64`).
4. Checks the raw kernel return value: on Linux, the kernel returns `-errno` (a negative integer between `-1` and `-4095`) on failure. The `glibc` wrapper converts any negative return value `ret` into **`errno = -ret`** and returns **`-1`** to your C code!

> **Important Exception:** Some `glibc` functions have **no** 1-to-1 system call of the same name—for example, `glibc` implements `getrlimit()` / `setrlimit()` by calling `prlimit64()` ([`getrlimit(2)`](https://man7.org/linux/man-pages/man2/getrlimit.2.html)), and implements `open()` by calling `openat(AT_FDCWD, ...)`. Conversely, newer Linux syscalls like `gettid()` (historically), `futex()`, or `io_uring_setup()` may lack a `glibc` wrapper on older distros and must be called via [`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html).

---

## 2. How System Calls Are Made

### A. Step-by-Step User-to-Kernel Transition
1. **User-space wrapper** loads arguments and the syscall number from `<sys/syscall.h>` (`__NR_*`, e.g., `__NR_gettid`, `__NR_write`).
2. **CPU trap instruction** switches privilege level and jumps to the kernel's registered entry point (`entry_SYSCALL_64` on `x86_64`).
3. **Kernel `sys_call_table` dispatch** validates the syscall number (`ENOSYS` if invalid, per [`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html)) and invokes the kernel C function (e.g., `ksys_write()`).
4. **Pointer validation**: The kernel **never** trusts user-space pointers—it copies data across the boundary safely using `copy_from_user()` / `copy_to_user()`, returning `-EFAULT` if your pointer points to unmapped or kernel memory (instead of crashing the kernel).
5. **Return to User Space**: The kernel restores registers and executes `sysret` (`sret` on `riscv64`), returning to `glibc`, which sets `errno` if needed.

### B. Architecture Calling Conventions ([`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html))
Notice that the kernel syscall ABI differs slightly from the normal C function ABI (for example, `x86_64` C functions pass the 4th argument in `%rcx`, whereas the `syscall` instruction clobbers `%rcx` with the return `%rip`, so the kernel uses **`%r10`** instead!):

| Architecture | Trap Instruction | Syscall # Register | Arg 1 – Arg 6 Registers | Return Value |
| :--- | :--- | :--- | :--- | :--- |
| **`x86_64`** | `syscall` | `%rax` | `%rdi`, `%rsi`, `%rdx`, **`%r10`**, `%r8`, `%r9` | `%rax` |
| **`riscv64`** | `ecall` | `a7` | `a0`, `a1`, `a2`, `a3`, `a4`, `a5` | `a0` |
| **`aarch64`** | `svc #0` | `x8` | `x0`, `x1`, `x2`, `x3`, `x4`, `x5` | `x0` |

### C. Invoking Syscalls Directly via `syscall(2)`
When a Linux kernel feature has no `glibc` wrapper (or when you want to test raw kernel behavior), include `<sys/syscall.h>` and `<unistd.h>`:
```c
#define _GNU_SOURCE
#include <sys/syscall.h>
#include <unistd.h>

/* Example: invoke gettid directly via SYS_gettid */
pid_t tid = (pid_t)syscall(SYS_gettid);
```
> **Note:** Remember from Chapter 5 that a small set of read-only syscalls (`clock_gettime`, `gettimeofday`, `time`, `getcpu`) are mapped into user space via [`vdso(7)`](https://man7.org/linux/man-pages/man7/vdso.7.html) to avoid the trap overhead entirely.

---

## 3. Return Values and Error Numbers (`errno`)

### A. How `errno` Works in Multi-Threaded C Programs
Per [`errno(3)`](https://man7.org/linux/man-pages/man3/errno.3.html):
- **Never write `extern int errno;`**! In modern `glibc`, `errno` is a macro expanding to a function call `(*__errno_location ())` that returns a **thread-local (`__thread`)** `int` pointer, making `errno` completely thread-safe across Pthreads.
- **Zero-out vs. Save Rule**:
  1. Successful syscalls **do not reset `errno` to `0`**—never inspect `errno` unless the syscall's return value explicitly indicated failure (usually `-1` or `NULL`).
  2. Subsequent library calls (even `printf()` or `free()`) can overwrite `errno` even when they succeed ([`perror(3)`](https://man7.org/linux/man-pages/man3/perror.3.html)). **Always save `int errsv = errno;` immediately** on error before calling any logger or cleanup function!

### B. `perror()` vs. `strerror()` vs. `strerror_r()` (The XSI vs. GNU Trap!)
As documented in [`strerror(3)`](https://man7.org/linux/man-pages/man3/strerror.3.html):
- **`perror(const char *s)`**: Prints `s: <message>\n` to `stderr`. Not thread-safe on all historical implementations and hardcodes `stderr`.
- **`strerror(int errnum)`**: Returns a pointer to a static buffer—**not thread-safe**!
- **`strerror_r(int errnum, char *buf, size_t buflen)`**: Thread-safe, **but has two incompatible signatures depending on `_GNU_SOURCE`**!
  - **XSI / POSIX version** (when `_POSIX_C_SOURCE >= 200112L` and `_GNU_SOURCE` is **not** defined): returns **`int`** (`0` on success, positive error or `-1` on failure).
  - **GNU version** (when **`_GNU_SOURCE` IS defined**): returns **`char *`**—and may return a pointer to an immutable static string *without* writing into your `buf`!
  - *(Modern `glibc >= 2.32` also added `strerrorname_np(errnum)` and `strerrordesc_np(errnum)`, which are both thread-safe and async-signal-safe per [`strerror(3)`](https://man7.org/linux/man-pages/man3/strerror.3.html)!)*

### C. Handling Transient Errors (`EINTR` and `EAGAIN`/`EWOULDBLOCK`)
In real Linux daemons, a return of `-1` does not always mean a fatal bug:
- **`EINTR`**: A blocking syscall (`read`, `write`, `waitpid`, `poll`, `epoll_wait`) was interrupted by a delivered signal before transferring data. Unless you intend to abort on the signal, **retry in a loop** (`while ((n = read(...)) == -1 && errno == EINTR)`).
- **`EAGAIN` / `EWOULDBLOCK`**: On non-blocking sockets or pipes (`O_NONBLOCK`), means *"no data ready right now; try again when `epoll` wakes you."*

---

## 4. Chapter 6 Hands-On Labs & `syswatch` Project Milestone 6

### Part A: Warm-Up Lab — Comparing `ltrace` vs. `strace` on Buffered I/O
Run this quick experiment in `/workdir` to watch `glibc` user-space buffering batch `stdio` calls into a single kernel `write()` syscall:
```bash
cat << 'EOF' > io_trace.c
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("Hello ");
    printf("from ");
    printf("stdio!\n");          /* Flushes line-buffered stdout on \n */
    write(STDOUT_FILENO, "Raw syscall\n", 12);
    return 0;
}
EOF

gcc -std=c11 -Wall -Wextra -O2 io_trace.c -o io_trace
ltrace -e printf,puts,fwrite,write ./io_trace
strace -e trace=write ./io_trace
```
Notice how `ltrace` sees multiple `printf`/`fwrite` library calls, while `strace` sees how `glibc` coalesced them before crossing into the kernel!

---

### Part B: Cumulative Mini-Project Milestone 6 — Safe Syscall Wrappers, `EINTR` Retry Loops & `strerror_r` Audit in `libsyswatch`

Now let's harden `libsyswatch` so every future daemon/networking milestone uses battle-tested, `EINTR`-safe system call wrappers and properly handles `_GNU_SOURCE` vs. XSI `strerror_r()`!

#### Your Implementation Tasks
1. **Audit & Harden `sw_strerror()` in `src/error.c`**:
   - Because `include/syswatch.h` defines `_GNU_SOURCE`, [`strerror(3)`](https://man7.org/linux/man-pages/man3/strerror.3.html) provides the **GNU-specific `strerror_r()`** returning `char *` (which may *not* write into `buf`!).
   - Make sure `sw_strerror(int errnum, char *buf, size_t buflen)` handles this cleanly:
     - Call `const char *msg = strerror_r(errnum, buf, buflen);` and copy/return `msg` safely (or `snprintf(buf, buflen, "%s", msg)` so `buf` is always populated if callers inspect `buf`), **without** clobbering `errno`!
     - Better yet: save `int saved_errno = errno;` on entry and restore `errno = saved_errno;` before returning so logging an error never mutates `errno`.
2. **Create `src/syscall_wrap.c` & Declare Helpers in `include/syswatch.h`**:
   - Add two foundational `SW_API` wrappers that you will reuse heavily in Chapters 13–31:
     - `SW_API ssize_t sw_read_retry(int fd, void *buf, size_t count);` — wraps `read()` and retries automatically if `errno == EINTR`.
     - `SW_API ssize_t sw_write_all(int fd, const void *buf, size_t count);` — handles **both** `EINTR` *and* **short/partial writes** (loops until all `count` bytes are written or a real error occurs, advancing `const uint8_t *p` on each iteration).
     - `SW_API pid_t sw_gettid(void);` — invokes `(pid_t)syscall(SYS_gettid)` directly via [`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html) so your logger can print the kernel thread ID (`TID`)!
   - Update `src/log.c` to include `[tid=<id>]` (using `sw_gettid()`) in every log line.
3. **Update `Makefile` & Verify with `strace` and `valgrind`/`asan`**:
   - Add `src/syscall_wrap.c` to `LIB_SRCS` in your `Makefile`.
   - Write a small unit check in `src/syswatchctl.c` (or a test target) that tests `sw_write_all()` to `STDOUT_FILENO` and logs an intentional `ENOENT` error using `sw_strerror()` while verifying `errno` is preserved.
   - Run `make -j$(nproc) all check valgrind-check asan-check` and `strace -e trace=gettid,write ./build/bin/syswatchd ./build/lib/plugin_dummy_cpu.so` to observe your `SYS_gettid` syscall in action!
