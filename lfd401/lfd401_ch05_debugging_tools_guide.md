# LFD401 Chapter 5: Debugging Tools — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Debugging Tools** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), examining high-resolution POSIX/Linux clocks and the `vDSO` fast path, CPU/memory profiling (`time`, `perf`, `gcov`), `valgrind`, and compiler-instrumented sanitizers (`AddressSanitizer`, `UBSan`, `ThreadSanitizer`), followed by **Milestone 5** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. Getting the Time in Linux Systems Programming

Timekeeping in Linux spans two distinct concepts: **wall-clock / elapsed time** (how long a user waits) and **CPU / resource time** (how much time the kernel scheduler actually gave your process or thread).

### A. POSIX Clocks & `clock_gettime()`
The modern, nanosecond-resolution POSIX API is [`clock_gettime(3)`](https://man7.org/linux/man-pages/man3/clock_gettime.3.html) (`#include <time.h>`, returning `struct timespec` with `tv_sec` + `tv_nsec`):

| `clockid_t` Constant | Behavior & Guarantees | When to Use in Linux Daemons |
| :--- | :--- | :--- |
| **`CLOCK_REALTIME`** | System-wide wall-clock time since the UNIX Epoch (`1970-01-01`). **Can jump forward or backward** if NTP/chrony adjusts the clock or an admin runs `date -s`. | Human-readable log timestamps and calendar dates—**never** for timeouts or latency measurement! |
| **`CLOCK_MONOTONIC`** | Monotonically increasing clock since an unspecified past point (boot). Never jumps backward; slewed smoothly by NTP frequency adjustments; pauses during system suspend. | **Default choice** for elapsed-time intervals, benchmarks, watchdog timers, and `epoll`/mutex timeouts. |
| **`CLOCK_MONOTONIC_RAW`** | Raw hardware oscillator frequency with zero NTP frequency slewing. | High-precision local micro-benchmarks immune to NTP drift adjustments. |
| **`CLOCK_BOOTTIME`** | Identical to `CLOCK_MONOTONIC`, except it **includes time spent while the system is suspended**. | Long-running daemon scheduling and uptime tracking on laptops/embedded devices that sleep. |
| **`CLOCK_PROCESS_CPUTIME_ID`** / **`CLOCK_THREAD_CPUTIME_ID`** | Measures actual CPU time consumed by the calling process or thread (excluding time spent sleeping or waiting on I/O). | Profiling CPU-bound functions inside a single process/thread. |

### B. Under the Hood: Why `clock_gettime()` Doesn't Show Up in `strace` (`vDSO`)
In traditional syscalls, entering kernel mode (`syscall` instruction) costs hundreds of CPU cycles. Because reading the clock is called millions of times per second by databases and daemons, the Linux kernel maps a tiny, read-only ELF shared library called the **`vDSO` (Virtual Dynamic Shared Object)**—documented in [`vdso(7)`](https://man7.org/linux/man-pages/man7/vdso.7.html)—directly into every user-space process's address space (`linux-vdso.so.1`).
- When you call `clock_gettime(CLOCK_MONOTONIC, &ts)`, `glibc` invokes `__vdso_clock_gettime` in user space, which reads the kernel's shared timekeeper data page and CPU TSC/timer registers **without a context switch into the kernel**.
- **Crucial Consequence ([`vdso(7)`](https://man7.org/linux/man-pages/man7/vdso.7.html))**: `clock_gettime()`, `gettimeofday()`, `time()`, and `getcpu()` executed via `vDSO` **will not appear in `strace` output** and cannot be intercepted by `seccomp` filters!

### C. Measuring Resource Usage: `getrusage(2)`
To measure user CPU time, kernel (`sys`) CPU time, page faults, and context switches from inside your C code, call [`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html) (`#include <sys/resource.h>`):
- `RUSAGE_SELF`: Totals for the calling process (all threads combined).
- `RUSAGE_CHILDREN`: Totals for all terminated and `wait()`-ed child processes.
- `RUSAGE_THREAD`: Linux-specific; totals for only the calling thread.
- Key fields in `struct rusage`: `ru_utime` (user CPU time), `ru_stime` (kernel CPU time), `ru_minflt` / `ru_majflt` (minor vs. disk-I/O major page faults), `ru_nvcsw` (voluntary context switches, e.g., waiting on I/O or a mutex), and `ru_nivcsw` (involuntary preemptions when your time slice expired).

---

## 2. Profiling and Performance Analysis

### A. Shell `time` Builtin vs. `/usr/bin/time`
As noted in [`time(1)`](https://man7.org/linux/man-pages/man1/time.1.html), typing bare `time ./app` in `bash` or `zsh` invokes the shell's builtin, not `/usr/bin/time`. Use `/usr/bin/time -v ./app` to get full `getrusage(2)` metrics:
- **`real`** (wall-clock elapsed time) vs. **`user` + `sys`** (CPU time). If `real >> user + sys`, your program is **I/O-bound or lock-contended** (sleeping). If `user + sys ≈ real` (or `> real` across multiple threads), your program is **CPU-bound**.
- `Maximum resident set size (kbytes)`: Peak physical RAM (`VmHWM`).
- `Minor` vs. `Major` page faults and `Voluntary` vs. `Involuntary` context switches.

### B. Linux `perf` (Hardware PMU & Kernel Sampling)
Built on [`perf_event_open(2)`](https://man7.org/linux/man-pages/man2/perf_event_open.2.html), `perf` samples the CPU's Hardware Performance Monitoring Unit (PMU) with minimal overhead (~1–3%):
```bash
# 1. Summarize IPC (Instructions Per Cycle), cache misses, and branch mispredictions:
perf stat ./build/bin/syswatchd

# 2. Sample call stacks (compile with -fno-omit-frame-pointer -O2 -g for clean graphs):
perf record -F 99 -g -- ./build/bin/syswatchd
perf report --stdio
```
*(Note: Inside unprivileged Docker containers, `perf_event_open(2)` may be blocked by the host kernel's `/proc/sys/kernel/perf_event_paranoid` or Docker's `CAP_PERFMON` restriction—when that happens, Valgrind's **Callgrind** gives you exact, deterministic instruction and cache profiling in user space!)*

### C. Callgrind (`valgrind --tool=callgrind`) & `gcov`/`lcov`
- **Callgrind ([Callgrind Manual](https://valgrind.org/docs/manual/cl-manual.html))**: Runs your binary on a synthetic CPU and records exact **Instruction Read (`Ir`)** counts and caller/callee edges into `callgrind.out.<pid>`. Annotate source hot spots directly in the terminal with:
  ```bash
  valgrind --tool=callgrind ./build/bin/syswatchd
  callgrind_annotate --auto=yes callgrind.out.<pid>
  ```
- **Code Coverage (`gcov` / `gcovr` / `lcov`)**: Compile with `--coverage` (`-fprofile-arcs -ftest-coverage` per [GCC Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)), run your test suite, and run `gcovr -r .` to verify which error-handling branches in your C code were never executed!

---

## 3. `valgrind` (Memcheck)

[`valgrind`](https://valgrind.org/docs/manual/mc-manual.html) is a dynamic binary instrumentation framework: it translates your compiled machine code into an intermediate representation (VEX IR) at runtime and executes it on a virtual CPU that tracks **validity bits (`V` bits)** and **addressability bits (`A` bits)** for every single byte of memory and every register!

Because it requires **zero recompilation** (though `-g` / `-Og` is strongly recommended for readable stack traces), it even checks pre-compiled third-party `.so` libraries. The trade-off is a **10x–30x runtime slowdown**.

### Essential Memcheck Flags & Leak Categories
```bash
valgrind --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --track-fds=yes \
  --error-exitcode=1 \
  ./build/bin/syswatchd
```
- **`--track-origins=yes`**: Essential when Memcheck warns *"Conditional jump or move depends on uninitialised value(s)"*—it traces the uninitialized byte back to the exact `malloc()` or stack frame where it was created.
- **`--track-fds=yes`**: Catches **leaked file descriptors** (files, sockets, pipes, or `dlopen` mappings left open at exit)—critical for long-running daemons like `syswatchd`!
- **Understanding Memcheck Leak Categories ([Memcheck Manual](https://valgrind.org/docs/manual/mc-manual.html))**:
  - **Definitely lost**: No pointer anywhere in memory points to the block—a true memory leak.
  - **Indirectly lost**: A leaked struct holds the only pointer to this sub-allocation; fixing the "definitely lost" root fixes this too.
  - **Possibly lost**: Only an interior pointer (pointing into the middle of a `malloc` block) was found.
  - **Still reachable**: A global/static pointer still points to the block at exit (not an active runtime leak, though clean daemons free resources on shutdown).

---

## 4. AddressSanitizer (`ASan`) & UndefinedBehaviorSanitizer (`UBSan`)

Unlike Valgrind's virtual-CPU emulation, **Sanitizers** ([GCC Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)) instruct `gcc` or `clang` to inject fast inline shadow-memory checks directly at compile time:
- **Speed**: Only ~**2x** slowdown (fast enough to run in CI and interactive testing), compared to Valgrind's 10x–30x.
- **What ASan (`-fsanitize=address`) Catches That Valgrind Cannot**:
  Because `malloc()` interceptors only wrap the heap, Valgrind Memcheck **cannot** detect **stack buffer overflows** or **global variable overflows**. ASan places poisoned **redzones** around *heap*, *stack*, and *global* objects, catching out-of-bounds reads/writes, **use-after-free**, **use-after-return**, and **double-free** immediately with colorized shadow-byte maps!
- **LeakSanitizer (`LSan`)**: Enabled automatically by `-fsanitize=address` on Linux (`x86_64`, `aarch64`, `riscv64`).
- **UndefinedBehaviorSanitizer (`-fsanitize=undefined`)**: Catches signed integer overflow, null-pointer dereferences, misaligned pointers, shift overflows, and division by zero.
- **ThreadSanitizer (`-fsanitize=thread`)**: Detects POSIX thread data races (cannot be combined with `-fsanitize=address` in the same binary).

### Recommended Sanitizer Compiler & Runtime Flags
```bash
# Compile AND link with sanitizer flags + frame pointers for clean stack traces
gcc -std=c11 -Wall -Wextra -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    src/app.c -o app_asan

# Control runtime behavior via environment variables:
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./app_asan
```

### Valgrind Memcheck vs. AddressSanitizer: When to Use Which?
| Capability | Valgrind (`memcheck`) | AddressSanitizer (`-fsanitize=address`) |
| :--- | :--- | :--- |
| **Recompilation Required?** | No (works on existing binaries & `.so` libs) | Yes (must compile and link with `-fsanitize=address`) |
| **Runtime Slowdown** | ~10x–30x | ~2x |
| **Stack & Global Overflows** | **No** | **Yes** |
| **Use of Uninitialized Memory** | **Yes** (`--track-origins=yes`) | **No** (requires Clang's MemorySanitizer) |
| **Leaked File Descriptors** | **Yes** (`--track-fds=yes`) | **No** |

---

## 5. Chapter 5 Hands-On Labs & `syswatch` Project Milestone 5

### Part A: Warm-Up Lab — Stack Overflow vs. Uninitialized Memory
Run this quick comparison in a scratch directory to see why experienced Linux engineers run **both** ASan and Valgrind:
1. Write a tiny `bug_test.c` with two functions:
   - `stack_oob()`: declares `int arr[4];` on the stack and writes `arr[4] = 99;`.
   - `uninit_branch()`: declares `int flag;` without initializing it and checks `if (flag) puts("yes");`.
2. Run it under `valgrind --track-origins=yes` (notices `uninit_branch`, **misses** `stack_oob`!), then compile with `-fsanitize=address` and run it (catches `stack_oob` immediately!).

---

### Part B: Cumulative Mini-Project Milestone 5 — High-Res Benchmarking, Sanitizer Targets & Leak-Clean `syswatch`

Let's upgrade `libsyswatch` with a monotonic microsecond/nanosecond timer helper, add automated `asan` and `valgrind` verification targets to your `Makefile`, and ensure `syswatchd` + `syswatchctl` are 100% leak-free (including zero leaked file descriptors or `dlopen` handles).

#### Your Implementation Tasks
1. **Add Monotonic Timing & Resource Snapshot Helpers (`src/timer.c` + `include/syswatch.h`)**:
   - Declare in `include/syswatch.h`:
     ```c
     SW_API uint64_t sw_now_ns(void);
     SW_API void sw_log_rusage(const char *label);
     ```
   - Implement `sw_now_ns()` in `src/timer.c` using `clock_gettime(CLOCK_MONOTONIC, &ts)` ([`clock_gettime(3)`](https://man7.org/linux/man-pages/man3/clock_gettime.3.html)) returning `(uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec`.
   - Implement `sw_log_rusage()` using `getrusage(RUSAGE_SELF, &ru)` ([`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html)) to log user CPU time, system CPU time, minor/major page faults, and peak RSS (`ru.ru_maxrss` KB).
   - Add `src/timer.c` to `LIB_SRCS` in your `Makefile`.
2. **Instrument `syswatchd` Plugin Execution**:
   - In `src/syswatchd.c`, measure the elapsed nanoseconds around `dlopen()` + `init()` + `collect()` using `sw_now_ns()`, log the plugin execution latency in microseconds (`%.2f us`), and call `sw_log_rusage("after plugin collect")` before exiting.
3. **Add `BUILD=asan` and `valgrind-check` Targets to `Makefile`**:
   - Add an `asan` build mode (`ifeq ($(BUILD),asan)`) that compiles and links with `-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all` (note: both `CFLAGS` and `LDFLAGS` need `-fsanitize=address,undefined` when linking executables!).
   - Add a `.PHONY` target `valgrind-check` that runs:
     ```makefile
     valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
       --track-origins=yes --track-fds=yes --error-exitcode=1 \
       ./build/bin/syswatchd ./build/lib/plugin_dummy_cpu.so
     ```
   - Add a `.PHONY` target `asan-check` that builds with `BUILD=asan` and runs `syswatchd` and `syswatchctl` under ASan/UBSan + LeakSanitizer.
4. **Verify Zero Leaks & Observe `vDSO`**:
   - Run `make valgrind-check` and `make asan-check`—fix any leaked `dlopen` handles, heap buffers, or unclosed file descriptors until both exit `0`.
   - Run `strace -e trace=clock_gettime ./build/bin/syswatchd` and verify that `clock_gettime` does **not** appear in the syscall trace because it ran in user space via `linux-vdso.so.1`!
