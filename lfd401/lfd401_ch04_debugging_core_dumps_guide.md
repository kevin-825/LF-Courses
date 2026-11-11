# LFD401 Chapter 4: Debugging and Core Dumps — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Debugging and Core Dumps** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), explaining how the GNU Debugger (`gdb`) controls processes via `ptrace`, what lives inside an ELF `ET_CORE` core dump, how the Linux kernel decides whether and where to write core files, and how to perform post-mortem crash forensics, followed by **Milestone 4** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. `gdb` — Architecture and Power Workflows

### How `gdb` Works Under the Hood on Linux
When you run `gdb ./syswatchd` or attach to a running daemon with `gdb -p <PID>`, `gdb` relies on two kernel and ELF mechanisms:
1. **`ptrace(2)` System Call**: Allows the tracer (`gdb`) to pause the tracee, read/write CPU registers (`PTRACE_GETREGS`/`PTRACE_SETREGS`), single-step machine instructions (`PTRACE_SINGLESTEP`), and patch software breakpoint instructions (`int3` / `0xcc` on `x86_64`, `ebreak` on `riscv64`) into the tracee's `.text` pages.
2. **DWARF Debugging Information**: Generated when you compile with `-g` or `-ggdb3` ([GCC Debugging Options](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html)). DWARF sections (`.debug_info`, `.debug_line`, `.debug_frame`, `.debug_macro`) map raw instruction addresses and stack offsets back to C source lines, data types, struct layouts, and `#define` macros.

### Essential `gdb` Commands Reference

| Category | GDB Command | What It Does & Systems Programming Pro-Tip |
| :--- | :--- | :--- |
| **Starting & Attaching** | `gdb --args ./syswatchd -v` | Launches `gdb` with CLI arguments pre-populated for `run`. |
| | `start` | Sets a temporary breakpoint at `main()` and runs—saving you from typing `b main` + `r`. |
| | `gdb -p <PID>` | Attaches to a live running process (requires `--cap-add=SYS_PTRACE` in Docker, which our container enables). |
| | `gdbserver :2345 ./app` | Runs lightweight stub on target; connect from host `gdb` via `target remote :2345` ([Connecting in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Connecting.html)). |
| **Breakpoints & Catchpoints** | `b src/log.c:45 if level == 3` | Conditional breakpoint ([Breakpoints in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Breakpoints.html)): stops only when `level == 3` evaluates true. |
| | `dprintf log.c:45, "lvl=%d\n", level` | **Dynamic `printf`**: injects a live trace print at line 45 without recompiling or stopping execution! |
| | `catch syscall openat exit_group` | Stops execution immediately upon entering and returning from specific Linux system calls. |
| **Hardware Watchpoints** | `watch global_counter` | Stops the instant any instruction **modifies** `global_counter` ([Set Watchpoints in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Set-Watchpoints.html)). Uses CPU hardware debug registers (`DR0`–`DR3` on x86) so execution runs at full native speed! |
| | `rwatch var` / `awatch var` | Breaks when `var` is **read** (`rwatch`) or **read/written** (`awatch`). Essential for catching silent buffer overflows that overwrite adjacent heap/stack variables. |
| | `watch -l ptr->field` | Watches the **memory location** (`-l`) pointed to by `ptr->field`, surviving even after the local variable `ptr` goes out of scope. |
| **Stack & Backtrace** | `bt` / `bt full` | Prints the call stack; `bt full` also dumps every local variable in every stack frame ([Backtrace in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html)). |
| | `frame N` (`f N`), `up`, `down` | Moves up/down the call stack so you can inspect caller variables and arguments (`info args`, `info locals`). |
| **Memory & Struct Layout** | `p *array_ptr@16` | Prints 16 contiguous elements of a dynamically allocated array (`malloc`). |
| | `ptype /o struct sw_metric_plugin` | Prints the exact **byte offset, size, and compiler padding holes** of every field in a `struct`! |
| | `x/16gx $rsp` / `x/s addr` | Examines raw memory (`x/<count><format><size>`, where size is `b`=1B, `h`=2B, `w`=4B, `g`=8B). |
| | `disassemble /m` | Prints assembly instructions interleaved with the corresponding C source lines. |
| **Forks & Threads** | `set follow-fork-mode child` | Tells `gdb` to follow the child process after `fork()` instead of the parent. |
| | `set detach-on-fork off` | Keeps **both** parent and child attached under `gdb` (`info inferiors`, `inferior 2` to switch). |
| | `thread apply all bt full` | Dumps complete stack traces and local variables across **every** POSIX thread simultaneously. |

---

## 2. What Are Core Dump Files?

A **core dump** is an **ELF file of type `ET_CORE`** written by the Linux kernel when a process terminates abnormally due to specific unhandled signals, as documented in [`core(5)`](https://man7.org/linux/man-pages/man5/core.5.html).

### What Lives Inside an `ET_CORE` File?
Unlike an executable (`ET_EXEC` / `ET_DYN`), a core file does not need to duplicate unchanged read-only code sections if the original binary is available—instead, it captures the process's **runtime state** inside two primary ELF program header types (`readelf -l core`):
1. **`PT_LOAD` Memory Segments**:
   Snapshots of the process's virtual memory regions: the user-space **stack** of every thread, the **heap** (`brk`/`mmap`), and writable **`.data`/`.bss`** segments.
   - Controlled per-process by the hexadecimal bitmask in `/proc/<pid>/coredump_filter` (default `0x33`, which dumps anonymous private mappings, anonymous shared mappings, ELF headers, and private huge pages, per [Core File Generation in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Core-File-Generation.html)).
2. **`PT_NOTE` Metadata Segments (`readelf -n core`)**:
   - **`NT_PRSTATUS`**: Exact CPU register state (`%rip`, `%rsp`, `%rbp`, `%rax`, etc.) for every thread at the moment of the crash, plus the fatal signal number.
   - **`NT_SIGINFO`**: The complete kernel `siginfo_t` structure, including the exact faulting virtual memory address (`si_addr`) and fault reason code (`si_code`, such as `SEGV_MAPERR` for unmapped address vs. `SEGV_ACCERR` for permission violation like writing to `.rodata`).
   - **`NT_PRPSINFO`**: Process UID/GID, PID/PPID, and command-line arguments.
   - **`NT_AUXV` & `NT_FILE`**: The kernel ELF auxiliary vector and the table of all memory-mapped files (`.so` shared libraries and their load addresses).

### Signals That Trigger a Core Dump by Default
Per [`signal(7)`](https://man7.org/linux/man-pages/man7/signal.7.html), signals whose default disposition is **`Core`** include:

| Signal | Typical Root Cause in C Systems Code |
| :--- | :--- |
| **`SIGSEGV`** (11) | Dereferencing `NULL` (`0x0`), use-after-free, stack overflow, or writing to read-only memory (`.rodata` string literal). |
| **`SIGABRT`** (6) | Calling `abort()`, failing an `assert()` check, or `glibc` detecting heap corruption (`double free or corruption`). |
| **`SIGBUS`** (7) | Unaligned memory access (on strict-alignment architectures), or accessing a `mmap()` region beyond the underlying file's end. |
| **`SIGFPE`** (8) | Integer division by zero (`x / 0`) or signed overflow trap (`INT_MIN / -1`). |
| **`SIGILL`** (4) | Executing corrupted machine instructions (e.g., jumping through a corrupted function pointer or overwritten stack return address). |
| **`SIGQUIT`** (3) | Pressing `Ctrl-\` in the terminal (or sending `kill -QUIT <PID>`) to intentionally force a running process to dump core and exit. |
| **`SIGSYS`** (31) | Executing a system call blocked by a `seccomp` filter (`SECCOMP_RET_TRAP` / `SECCOMP_RET_KILL_PROCESS`). |

---

## 3. Producing Core Dumps

By default on most Linux distributions, the soft limit for core file size (`RLIMIT_CORE`) is set to **`0`**, which suppresses core dump creation completely.

### A. Enabling Core Dumps via Shell vs. Programmatically via `setrlimit(2)`
1. **In your interactive shell (inherited by child processes)**:
   ```bash
   ulimit -c            # Check current soft limit (0 = disabled)
   ulimit -c unlimited  # Allow unlimited core file size
   ```
2. **Programmatically inside a C daemon using [`getrlimit(2)` / `setrlimit(2)`](https://man7.org/linux/man-pages/man2/getrlimit.2.html)**:
   Production daemons like `syswatchd` shouldn't rely on whatever shell launched them—they configure `RLIMIT_CORE` directly during startup and call `prctl(PR_SET_DUMPABLE, 1L)` ([`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html)) so core dumps remain enabled even if the daemon drops privileges:
   ```c
   #include <sys/resource.h>
   #include <sys/prctl.h>

   int sw_enable_core_dumps(void) {
       struct rlimit rlim;
       if (getrlimit(RLIMIT_CORE, &rlim) < 0)
           return -1;
       rlim.rlim_cur = rlim.rlim_max; /* Raise soft limit to hard ceiling (usually RLIM_INFINITY) */
       if (setrlimit(RLIMIT_CORE, &rlim) < 0)
           return -1;
       if (prctl(PR_SET_DUMPABLE, 1L, 0L, 0L, 0L) < 0)
           return -1;
       return 0;
   }
   ```

### B. Controlling Core File Naming (`/proc/sys/kernel/core_pattern`)
Per [`core(5)`](https://man7.org/linux/man-pages/man5/core.5.html), the kernel reads `/proc/sys/kernel/core_pattern` to format the output filename (or pipe the core dump to a crash handler program if the first character is `|`):
- `%e` = executable filename, `%p` = PID, `%u` = UID, `%s` = signal number, `%t` = UNIX epoch timestamp.
- **Important Container Note**: `/proc/sys/kernel/core_pattern` is a **host-kernel global sysctl** (not namespaced per container). Inspect your container's active setting with:
  ```bash
  cat /proc/sys/kernel/core_pattern
  ```
  *(If your host OS pipes `core_pattern` to `|/usr/lib/systemd/systemd-coredump` or `|/usr/share/apport/apport`, raw `core` files on disk can also be generated at any time using `gcore` or inside `gdb` via `generate-core-file`, which writes directly to your current working directory without touching `/proc/sys/kernel/core_pattern`!)*

### C. Non-Destructive Live Core Dumps (`gcore`)
As noted in [`core(5)`](https://man7.org/linux/man-pages/man5/core.5.html) and [Core File Generation in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Core-File-Generation.html), you can snapshot a live, hung, or misbehaving daemon **without terminating it**:
```bash
gcore -o /tmp/syswatchd.live <PID>
```

---

## 4. Examining Core Dumps

### Post-Mortem Forensics Workflow
Once you have an executable compiled with `-g` / `-ggdb3` and a `core` file:
```bash
# 1. Verify the core file's ELF metadata and which command/signal produced it
file core*
readelf -n core*

# 2. Load both the unstripped executable and the core file into GDB
gdb ./build/bin/syswatchd ./core*
```

Inside `gdb`, run this 5-step forensic checklist:
1. **`bt full`**: Immediately shows the exact crashing function, file, line number, and the values of all local variables and arguments in every stack frame ([Backtrace in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html)).
2. **`p $_siginfo`**: Prints the kernel's `NT_SIGINFO` struct from the core file:
   - Check `$_siginfo._sifields._sigfault.si_addr` to see the exact memory address the CPU tried to access when it faulted.
3. **`info registers` (`i r`) & `disassemble /m`**: Shows the instruction pointer (`$rip` or `$pc`) and which CPU register held the bad pointer.
4. **`info proc mappings` & `info sharedlibrary`**: Lists every virtual memory address range and loaded `.so` (including your `dlopen()` plugins!) at the moment of the crash.
5. **`thread apply all bt full`**: Checks what every other thread was doing when the crash occurred.

### Stripped Production Binaries & Separate Debug Files (`debuglink`)
In production or embedded deployments where shipping `-ggdb3` symbols inside `/usr/sbin/syswatchd` wastes disk space, you compile once with `-O2 -ggdb3` and split the DWARF symbols into a separate `.debug` file before stripping the binary:
```bash
objcopy --only-keep-debug build/bin/syswatchd build/bin/syswatchd.debug
strip --strip-debug --strip-unneeded build/bin/syswatchd
objcopy --add-gnu-debuglink=build/bin/syswatchd.debug build/bin/syswatchd
```
When a customer or field device sends back a `core` file from the tiny stripped `syswatchd`, `gdb` automatically loads `syswatchd.debug` and gives you full source lines and local variables!

---

## 5. Chapter 4 Hands-On Labs & `syswatch` Project Milestone 4

### Part A: Warm-Up Lab — Catching Silent Memory Corruption with Hardware Watchpoints
Run this quick lab in a scratch file to see why hardware watchpoints ([Set Watchpoints in GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Set-Watchpoints.html)) find bugs in 5 seconds that take hours of `printf` debugging:
```bash
cat << 'EOF' > corrupt.c
#include <stdio.h>
struct state {
    int buffer[4];
    int is_authenticated;
} g_state = { .buffer = {0}, .is_authenticated = 0 };

void buggy_fill(int count) {
    for (int i = 0; i <= count; i++) { /* Off-by-one bug! */
        g_state.buffer[i] = 0xdeadbeef;
    }
}
int main(void) {
    buggy_fill(4);
    printf("auth = 0x%x\n", g_state.is_authenticated);
    return 0;
}
EOF

gcc -std=c11 -Wall -Wextra -O0 -ggdb3 corrupt.c -o corrupt
gdb -batch \
  -ex "start" \
  -ex "watch g_state.is_authenticated" \
  -ex "continue" \
  -ex "bt full" \
  ./corrupt
```

---

### Part B: Cumulative Mini-Project Milestone 4 — Crash Dump Manager & Post-Mortem Forensics in `syswatch`

Let's upgrade `libsyswatch` and `syswatchd` so your daemon configures its own core-dump resource limits at startup and supports controlled crash-forensics testing.

#### Your Implementation Tasks
1. **Add `sw_enable_core_dumps()` to `libsyswatch` (`src/crash.c` + `include/syswatch.h`)**:
   - Declare `SW_API int sw_enable_core_dumps(void);` in `include/syswatch.h`.
   - Implement it in `src/crash.c` using [`getrlimit(2)` / `setrlimit(2)`](https://man7.org/linux/man-pages/man2/getrlimit.2.html) (`RLIMIT_CORE`) and [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html) (`PR_SET_DUMPABLE, 1L`).
   - Log the previous and new `RLIMIT_CORE` soft/hard limits using `SW_LOG_INFO(...)`, and use your `sw_strerror()` helper if either syscall fails.
   - Add `src/crash.c` to `LIB_SRCS` in your `Makefile` so both `libsyswatch.a` and `libsyswatch.so.1.0.0` include it automatically.
2. **Add a `--crash-test` Flag to `src/syswatchd.c`**:
   - Call `sw_enable_core_dumps()` during `syswatchd` startup.
   - If `syswatchd` is invoked with `--crash-test`, call a helper function `static void trigger_test_crash(const char *plugin_name)` that creates a local struct on the stack, intentionally dereferences a bad pointer (e.g., `volatile int *bad_ptr = (volatile int *)0xdead0000; *bad_ptr = 42;`), and triggers `SIGSEGV`.
3. **Add a `dump-check` Target to Your `Makefile` & Inspect the Core File**:
   - Check `/proc/sys/kernel/core_pattern` in your container:
     - If it writes `core*` files directly, run `./build/bin/syswatchd --crash-test` (with `ulimit -c unlimited`) to generate a real kernel core dump.
     - If your Docker host intercepts `core_pattern` via `systemd-coredump`/`apport`, run `gdb -batch -ex "b trigger_test_crash" -ex "run --crash-test" -ex "generate-core-file build/syswatchd.core" ./build/bin/syswatchd` to produce `build/syswatchd.core`.
   - Inspect your core dump with:
     ```bash
     readelf -n build/syswatchd.core
     gdb -batch \
       -ex "bt full" \
       -ex "p \$_siginfo._sifields._sigfault.si_addr" \
       -ex "info registers" \
       ./build/bin/syswatchd build/syswatchd.core
     ```
   - Verify that `$_siginfo._sifields._sigfault.si_addr` shows `0xdead0000` and `bt full` displays your local stack variables inside `trigger_test_crash()`!
