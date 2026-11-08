# LFD401 Chapter 1: Compilers — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Compilers** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), examining how the GNU Compiler Collection (`gcc`), the C preprocessor (`cpp`), and LLVM/Clang transform C source code into Linux ELF binaries, along with your first cumulative project milestone for **`syswatchd` + `syswatchctl`**.

---

## 1. GCC: Architecture and the 4-Stage Compilation Pipeline

When you run `gcc` on Linux, you are invoking a **compiler driver**—not a single monolithic binary. As documented in the [GCC Overall Options Manual](https://gcc.gnu.org/onlinedocs/gcc-13.5.0/gcc/Overall-Options.html), the `gcc` driver orchestrates a four-stage pipeline of underlying tools (`cc1`, `as`, `collect2`/`ld`).

| Stage | Flag to Stop Here | Underlying Tool Invoked | Input → Output File | What Happens Under the Hood |
| :--- | :--- | :--- | :--- | :--- |
| **1. Preprocessing** | `gcc -E` | `cc1 -E` (integrated `cpp`) | `main.c` → `main.i` | Strips comments, expands `#include` headers recursively, evaluates `#ifdef`/`#if` conditionals, and expands `#define` macros into a single translation unit. |
| **2. Compilation Proper** | `gcc -S` | `cc1` (C front/middle/back end) | `main.i` → `main.s` | Parses C syntax into an Abstract Syntax Tree (AST), lowers to **GIMPLE** and **SSA** (Static Single Assignment) for machine-independent optimization, lowers to **RTL** (Register Transfer Language), and emits target-specific assembly (`x86_64` or `riscv64`). |
| **3. Assembly** | `gcc -c` | `as` (GNU Assembler) | `main.s` → `main.o` | Translates human-readable assembly mnemonics into machine code instructions inside a relocatable ELF object file (`.text`, `.data`, `.rodata`, `.bss`, and unresolved symbol tables). |
| **4. Linking** | *(default)* | `collect2` → `ld` (GNU Linker) | `main.o` + `libc.so` → `a.out` | Merges section tables across `.o` files, resolves external symbol references against static/shared libraries, injects the C runtime startup objects (`crt1.o`, `crti.o`, `crtn.o` which call `_start` → `__libc_start_main` → `main`), and writes the final ELF executable. |

### Inspecting the Pipeline in Your Container
- **See every hidden sub-command**: Pass `-v` (verbose) or `-###` (dry-run with quoted arguments, per [GCC Overall Options](https://gcc.gnu.org/onlinedocs/gcc-3.2.1/gcc/Overall-Options.html)) to watch `gcc` invoke `cc1`, `as`, and `collect2`:
  ```bash
  gcc -### hello.c -o hello
  ```
- **Keep all intermediate files without running 4 separate commands**: Use `-save-temps=obj` (documented in [GCC Overall Options](https://gcc.gnu.org/onlinedocs/gcc-13.5.0/gcc/Overall-Options.html)):
  ```bash
  gcc -save-temps=obj -Wall -O2 hello.c -o hello
  ls -l hello.i hello.s hello.o hello
  ```
- **Speed up compilation with pipes**: Adding `-pipe` tells `gcc` to pipe data between `cc1` and `as` in memory rather than writing temporary files to `/tmp`.

---

## 2. Other Compilers on Linux

While GCC is the historical standard for Linux systems programming, modern Linux environments rely heavily on a second major toolchain ecosystem alongside specialized compilers:

| Compiler Toolchain | CLI Command | Key Architectural Strengths & Linux Use Cases |
| :--- | :--- | :--- |
| **LLVM / Clang** | `clang`, `clang++`, `ld.lld` | Modular architecture documented in the [Clang Compiler User's Manual](https://clang.llvm.org/docs/UsersManual.html). Compiles C to **LLVM IR** before machine code generation. Offers drop-in GCC flag compatibility, fast compilation, expressive diagnostics, first-class static analysis (`clang-tidy`, `clangd`), and full support for building the mainline Linux kernel (`make LLVM=1`). |
| **Cross-Compilers (GCC / Clang)** | `riscv64-linux-gnu-gcc`, `aarch64-linux-gnu-gcc`, `clang --target=...` | Runs on an `x86_64` host to emit ELF binaries for another CPU architecture (such as `riscv64` or `arm64`). While GCC requires a separate build per target triplet, a single `clang` binary is natively a multi-target cross-compiler when paired with target sysroots (`--sysroot`). |
| **Intel oneAPI DPC++/C++ (`icx`)** | `icx` | LLVM-based compiler tuned for Intel Xeon/Core vectorization (AVX-512, AMX) and high-performance computing workloads. |
| **TinyCC (`tcc`)** | `tcc` | Ultra-fast, minimal footprint C compiler (~100 KB) capable of running C source files directly like scripts (`tcc -run script.c`), though it performs minimal optimization. |

---

## 3. Major `gcc` Options for Systems Programming

Systems programming requires strict control over C dialect standards, feature test macros, warnings, optimization passes, and debug metadata.

### A. C Standards & POSIX/GNU Feature Test Macros
By default, modern GCC compiles with `-std=gnu17` or `-std=gnu23` (ISO C plus GNU extensions).
- **Strict ISO C**: `-std=c99`, `-std=c11`, `-std=c17`, `-std=c23` (or `-pedantic-errors` to reject non-ISO GNU extensions).
- **GNU C Dialect**: `-std=gnu11`, `-std=gnu17` (enables GNU extensions such as `typeof`, statement expressions `({ ... })`, zero-length arrays, and `asm` blocks used heavily in the Linux kernel).
- **Critical Linux Pitfall — Feature Test Macros (`man 7 feature_test_macros`)**:
  When you pass strict standard flags like `-std=c11`, `glibc` defines `__STRICT_ANSI__` (see [GCC Predefined Macros](https://gcc.gnu.org/onlinedocs/gcc-4.2.2//cpp/Common-Predefined-Macros.html)) and **hides** POSIX and Linux-specific system call declarations (`sigaction`, `strndup`, `epoll_create1`, `splice`, `O_CLOEXEC`) inside standard headers!
  To expose POSIX and Linux APIs when compiling with `-std=c11`, always define feature test macros **before any `#include`** or via `-D` on the command line:
  ```c
  #define _GNU_SOURCE          /* Exposes POSIX.1-2008 + Linux-specific syscalls (clone, splice, epoll) */
  /* OR for strictly portable POSIX code: */
  #define _POSIX_C_SOURCE 200809L
  ```

### B. Warning & Static Diagnostic Flags
As detailed in [GCC Warning Options](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html), `-Wall` does **not** enable all warnings—it only enables warnings with a very low false-positive rate. Some data-flow warnings (such as `-Wuninitialized`) require optimization (`-O1` or higher) to compute control-flow graphs.

| Flag | Purpose in Systems C Code |
| :--- | :--- |
| `-Wall` | Enables core warnings: unused variables (`-Wunused`), missing return statements (`-Wreturn-type`), uninitialized variables, and `printf`/`scanf` format mismatches (`-Wformat`). |
| `-Wextra` | Enables additional checks not in `-Wall`: signed/unsigned comparison (`-Wsign-compare`), unused function parameters (`-Wunused-parameter`), and missing field initializers. |
| `-Werror` | Treats every warning as a hard compilation failure. Use `-Wno-error=unused-parameter` to downgrade specific warnings during active development. |
| `-Wformat=2 -Wformat-security` | Guards against format-string vulnerabilities where untrusted user input is passed as the format string to `printf(user_buf)` or `syslog()`. |
| `-Wshadow` | Warns when a local variable shadows another local variable, parameter, or global declaration—a classic source of subtle C bugs. |
| `-Wconversion` | Warns on implicit type conversions that may alter values (e.g., assigning `ssize_t` to `int` or `int` to `size_t`). |
| `-Wstrict-prototypes` | Warns if a function is declared or defined without specifying argument types (in C, `int foo()` means "unspecified arguments", whereas `int foo(void)` means "zero arguments"). |

### C. Optimization Options (`-O`)
Per [GCC Optimize Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html), if multiple `-O` flags appear on a command line, **the last `-O` flag wins**.

| Flag | Optimization Behavior & Trade-offs |
| :--- | :--- |
| `-O0` | Default. No optimization; fastest compile time; every C variable lives on the stack frame so `gdb` inspection matches source code 1-to-1. |
| `-Og` | Optimize debugging experience. Enables basic dead-code elimination and data-flow analysis (so `-Wuninitialized` works) without mangling line numbers or eliminating local variables needed by `gdb`. |
| `-O2` | **Standard production level** (used by the Linux kernel and Ubuntu packages). Enables inlining, partial redundancy elimination (PRE), instruction scheduling, and dead-store elimination without aggressive code-size bloat. |
| `-O3` | Aggressive speed optimization: loop unrolling, aggressive function cloning, and SIMD auto-vectorization. Can increase binary size and expose latent Undefined Behavior (UB) bugs (such as strict-aliasing violations under `-fstrict-aliasing`). |
| `-Os` | Optimize for small binary size (enables `-O2` passes that do not increase code size—ideal for embedded systems and bootloaders). |
| `-march=native` | Generates instructions tuned for the exact host CPU microarchitecture. |

### D. Debugging & Runtime Sanitizer Flags
Per [GCC Debugging Options](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html):
- `-g` / `-g2`: Emits native **DWARF** debugging sections (`.debug_info`, `.debug_line`, `.debug_str`) inside the ELF binary so `gdb`, `valgrind`, and `perf` can map machine addresses back to C source files, line numbers, and local variables. Does **not** alter the generated machine instructions!
- `-ggdb3`: Emits maximum GDB-specific DWARF level 3 metadata, **including `#define` preprocessor macros**, allowing you to run `info macro MY_MACRO` or `p MY_MACRO(x)` live inside `gdb`.
- `-fno-omit-frame-pointer`: Preserves the frame pointer register (`%rbp` on `x86_64`, `s0`/`fp` on `riscv64`) so `perf` and crash backtraces can unwind the call stack reliably even at `-O2`.
- `-fsanitize=address,undefined`: Injects compiler instrumentation for **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)** to catch out-of-bounds heap/stack accesses, use-after-free, and signed integer overflow immediately at runtime.

### E. Directory & Linker Options
Per [GCC Directory Options](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html) and [GCC Link Options](https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html):
- `-I/path/to/include`: Prepends `dir` to the `#include` header search path.
- `-L/path/to/lib`: Adds `dir` to the linker's search path for `-l` libraries.
- `-lname`: Links against `libname.so` (or `libname.a` if `-static` is specified). **Order matters to the GNU linker**: `-l` flags must be placed **after** the `.c` or `.o` files that reference symbols in that library (`gcc main.o -lm`, never `gcc -lm main.o`).
- `-Wl,option`: Forwards comma-separated options directly to `ld` (e.g., `-Wl,-rpath,/opt/syswatch/lib` or `-Wl,-Map=output.map`).

### F. Essential GNU C `__attribute__` Extensions for Systems Programming
As documented in [GCC Function Attributes](https://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html), GNU C attributes let you give the compiler extra semantic contracts:

```c
/* 1. Type-check custom variadic logger calls just like printf() */
void sw_log(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* 2. Tell compiler a function never returns (suppresses false missing-return warnings) */
void sw_fatal_exit(const char *msg)
    __attribute__((noreturn));

/* 3. Prevent struct padding when mapping binary network/Netlink/file headers */
struct __attribute__((packed)) wire_header {
    uint8_t  magic;
    uint32_t payload_len;
    uint16_t flags;
};

/* 4. RAII-style automatic cleanup when a local variable goes out of scope */
static inline void close_fd(int *fd) {
    if (*fd >= 0) close(*fd);
}
#define AUTO_CLOSE __attribute__((cleanup(close_fd)))
```

---

## 4. The C Preprocessor (`cpp`)

The preprocessor transforms source text before the C compiler parser ever sees it.

### Key Preprocessor Commands & Techniques
- **Inspecting Predefined Macros**: As noted in [Common Predefined Macros](https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html), you can dump every built-in and architecture-specific macro defined by `gcc` using `-dM -E`:
  ```bash
  gcc -dM -E - < /dev/null | grep -E '(__linux__|__x86_64__|__riscv|__GNUC__|__STDC_VERSION__)'
  ```
- **Command-Line Macro Injection (`-D` and `-U`)**:
  - `-DSYSWATCH_VERSION=\"0.1.0\"` defines a macro with a value.
  - `-DDEBUG` defines `DEBUG` with value `1`.
  - `-UDEBUG` undefines `DEBUG`.
- **Standard Diagnostic Macros**: `__FILE__` (source filename string), `__LINE__` (integer line number), `__DATE__`, `__TIME__`, and the C99 function-local identifier `__func__` (essential for structured daemon logging).
- **Stringification (`#`) and Token Pasting (`##`)**:
  - `#x` turns a macro argument into a string literal: `#define STR(x) #x` (`STR(SIGTERM)` → `"SIGTERM"`).
  - `a ## b` concatenates two tokens into a single identifier: `#define REG_FIELD(prefix, name) prefix ## _ ## name`.
- **Safe Multi-Statement Macros (`do { ... } while (0)`)**:
  Never write a multi-statement function-like macro with bare braces `{ ... }`, or `if (cond) MY_MACRO(x); else ...` will fail to compile due to the trailing semicolon. Always wrap multi-statement macros in `do { ... } while (0)` and parenthesize every parameter:
  ```c
  #define SW_LOG_ERR(fmt, ...)                                              \
      do {                                                                  \
          sw_log_write(SW_LOG_ERROR, __FILE__, __LINE__, __func__,          \
                       (fmt), ##__VA_ARGS__);                               \
      } while (0)
  ```

---

## 5. IDEs and CLI Code Navigation on Linux

In Linux systems and kernel development, you often work inside containers, remote SSH servers, or headless targets where heavy GUI IDEs aren't running directly. Here is how experienced Linux developers navigate C codebases:

1. **`universal-ctags` + `cscope` (Installed in Your Container)**:
   - Run `ctags -R .` in your project root to index every function, `struct`, `typedef`, and `#define`. In `vim`, press `Ctrl-]` on any symbol to jump to its definition and `Ctrl-t` to pop back.
   - Run `cscope -Rbq` and `cscope -d` to query *"Who calls this function?"*, *"Where is this global symbol modified?"*, or *"Where is this header included?"* across millions of lines of C code.
2. **`clangd` + `compile_commands.json`**:
   - Modern editors (VS Code Remote-Containers, Neovim LSP, Emacs Eglot) use `clangd`, which reads a `compile_commands.json` file generated by `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` or `bear -- make` to provide instant jump-to-definition, macro expansion hover, and inline `clang-tidy` diagnostics.
3. **GDB Text User Interface (`gdb -tui`)**:
   - Gives you a split-screen source-code, assembly, and register viewer right inside `tmux` or `zsh` (`layout split`, `layout regs`) without needing a GUI debugger.

---

## 6. Chapter 1 Hands-On Lab & `syswatch` Project Milestone 1

### Part A: Warm-Up Compiler & Preprocessor Experiments (Run in Your Container)
Run these two quick experiments in `/workdir` to see `gcc` and `cpp` internals firsthand:

1. **Observe `-g3` Macro Preservation vs `-O2` Dead-Code Elimination**:
   ```bash
   cat << 'EOF' > test_pipeline.c
   #define SCALE_FACTOR 42
   int compute(int x) {
       int dead_var = 999;
       return x * SCALE_FACTOR;
   }
   int main(void) { return compute(2); }
   EOF

   # 1. Inspect the 4 stages with -save-temps=obj
   gcc -save-temps=obj -Wall -Wextra -O2 -ggdb3 test_pipeline.c -o test_pipeline
   cat test_pipeline.s   # Notice how dead_var is completely gone and compute() is folded!

   # 2. Verify that -ggdb3 preserved #define macros inside GDB
   gdb -batch -ex "info macro SCALE_FACTOR" ./test_pipeline
   ```

2. **Observe Why `-O` Level Affects `-Wuninitialized`**:
   - Compile a function with a conditionally uninitialized variable at `-O0 -Wall` vs `-O2 -Wall` and see how GCC's control-flow analysis detects latent bugs when optimization is enabled.

---

### Part B: Cumulative Mini-Project Milestone 1 — Bootstrap `syswatch` Core & Diagnostic Logger

Now let's write the very first building block of **`syswatchd` + `syswatchctl`**. Every system daemon and CLI tool needs a common header and diagnostic logging module that prints structured timestamps, log levels, source file (`__FILE__`), line (`__LINE__`), function (`__func__`), and human-readable `errno` strings.

#### Your Directory Structure to Create in `/workdir/syswatch`
```text
/workdir/syswatch/
├── include/
│   └── syswatch.h
└── src/
    ├── log.c
    └── main_test.c
```

#### Your Implementation Requirements
1. **`include/syswatch.h`**:
   - Protect the header with standard `#ifndef SYSWATCH_H` / `#define SYSWATCH_H` / `#endif` include guards.
   - Define `_GNU_SOURCE` at the top if not already defined.
   - Define `SYSWATCH_VERSION` using `#ifndef` so it defaults to `"0.1.0-dev"` unless overridden at compile time via `-DSYSWATCH_VERSION=\"...\"`.
   - Define an `enum sw_log_level { SW_LOG_DEBUG = 0, SW_LOG_INFO, SW_LOG_WARN, SW_LOG_ERROR }`.
   - Declare the core logging function and attach the GCC `format(printf, ...)` attribute so the compiler type-checks every format string at compile time:
     ```c
     void sw_log_internal(enum sw_log_level level, const char *file, int line,
                          const char *func, const char *fmt, ...)
         __attribute__((format(printf, 5, 6)));
     ```
   - Create safe `do { ... } while (0)` wrapper macros `SW_LOG_INFO(fmt, ...)`, `SW_LOG_WARN(fmt, ...)`, `SW_LOG_ERROR(fmt, ...)`, and `SW_LOG_DEBUG(fmt, ...)` that automatically pass `__FILE__`, `__LINE__`, and `__func__` to `sw_log_internal()`.
   - Use `#ifdef ENABLE_DEBUG_LOG` so that when `ENABLE_DEBUG_LOG` is **not** defined at compile time, `SW_LOG_DEBUG(fmt, ...)` compiles down to a zero-cost no-op `do { (void)sizeof(printf(fmt, ##__VA_ARGS__)); } while (0)` (bonus points: notice how `(void)sizeof(...)` still makes GCC type-check the arguments without emitting any runtime instructions!).
2. **`src/log.c`**:
   - Implement `sw_log_internal()` using `<stdarg.h>` (`va_list`, `va_start`, `vfprintf`, `va_end`) and `<time.h>` (`clock_gettime` or `time`/`localtime_r`/`strftime`) to print lines formatted like:
     `[2026-11-03 01:45:00] [INFO ] [main_test.c:14:main] syswatch v0.1.0-dev starting...`
3. **`src/main_test.c`**:
   - Write a short `main(void)` that logs messages at all 4 levels (`DEBUG`, `INFO`, `WARN`, `ERROR`), plus an intentional test with a mismatched format specifier (e.g., passing an `int` to `%s`) commented out so you can test that `__attribute__((format(printf, 5, 6)))` catches it!
4. **Compile and Verify**:
   - Compile `src/log.c` and `src/main_test.c` into individual `.o` object files using `-c`, strict flags (`-std=c11 -Wall -Wextra -Werror -Wshadow -Wformat=2 -O2 -ggdb3 -Iinclude`), and `-DENABLE_DEBUG_LOG -DSYSWATCH_VERSION=\"0.1.0-alpha\"`, then link them into `./syswatch_test`.
   - Compile a second binary `./syswatch_release` **without** `-DENABLE_DEBUG_LOG` and run `objdump -d` or `nm` to verify the debug log call disappeared.
