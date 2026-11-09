# LFD401 Chapter 2: Libraries — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Libraries** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), detailing how static archives (`.a`), Position-Independent Code (`-fPIC`) shared objects (`.so`), the GNU linker (`ld`), the Linux dynamic loader (`ld.so`), and runtime dynamic loading (`dlopen`) work under the hood, followed by **Milestone 2** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. Static Libraries (`.a`)

A **static library** (`libname.a`) is not an executable or a single combined ELF binary—it is a standard UNIX **`ar` archive** containing a collection of individual relocatable ELF object files (`.o`) prepended with a global symbol lookup index, as documented in the [GNU Binary Utilities `ar` Manual](https://sourceware.org/binutils/docs/binutils/ar.html).

### How Static Linking Works at Build Time
When you link an executable against `libfoo.a`, the GNU linker ([GNU `ld`](https://sourceware.org/binutils/docs/ld.html)) scans the archive's symbol index and **copies only the specific `.o` member files that satisfy currently unresolved symbols** directly into your final executable.

| Characteristic | Static Library (`.a`) Behavior |
| :--- | :--- |
| **Naming Convention** | `lib<name>.a` (e.g., `libsyswatch.a`, `libc.a`) |
| **Creation Tool** | `ar rcs libsyswatch.a log.o error.o` (`r` = insert/replace, `c` = create silently, `s` = write `ranlib` symbol index) |
| **Inspection Tools** | `ar tv libsyswatch.a` (list `.o` members), `nm -s libsyswatch.a` ([GNU `nm` Manual](https://sourceware.org/binutils/docs/binutils/nm.html) — show symbol index + per-object symbols) |
| **Runtime Dependency** | Zero dependency on the `.a` file once the executable is linked; code is baked into the executable's `.text` and `.data` segments. |
| **Memory & Security Trade-off** | Every running process gets its own duplicate copy of library code in RAM/disk, and security fixes in a static library require recompiling and redeploying every executable. |

### The Classic Static Linker Trap: Single-Pass Left-to-Right Resolution
As emphasized in [GCC Link Options](https://gcc.gnu.org/onlinedocs/gcc-3.3.6/gcc/Link-Options.html), `ld` processes `.o` files and `.a` archives **once from left to right**, maintaining a list of currently unresolved (`U`) symbols:
- **Broken command**: `gcc -L. -lsyswatch main.o -o app`
  When `ld` sees `-lsyswatch` first, no symbols are unresolved yet, so `ld` extracts **zero** object files from `libsyswatch.a`. Then `ld` reads `main.o` and fails with `undefined reference to sw_log_internal`.
- **Correct command**: Always place libraries **after** the object files that call them:
  ```bash
  gcc main.o -L. -lsyswatch -o app
  ```
- **Circular static dependencies**: If `liba.a` calls `libb.a` and `libb.a` calls `liba.a`, use `-Wl,--start-group liba.a libb.a -Wl,--end-group` (per [GNU LD Options](https://sourceware.org/binutils/docs-2.29/ld/Options.html)) to instruct `ld` to rescan the group until no new symbols are resolved.

---

## 2. Shared Libraries (`.so`)

A **shared library** (Dynamic Shared Object, or **DSO**) is a single ELF binary that can be mapped into the virtual address spaces of multiple independent processes simultaneously by the dynamic linker ([`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html)).

### Why `-fPIC` (Position-Independent Code) Is Mandatory
In virtual memory with ASLR (Address Space Layout Randomization), `libsyswatch.so` might be mapped at virtual address `0x7f9a40000000` in `syswatchd` and at `0x7f1120000000` in `syswatchctl`.
- If the library's machine instructions contained hard-coded absolute memory addresses, the dynamic loader would have to patch (relocate) the `.text` code pages at load time—triggering Copy-on-Write (CoW) page faults and destroying the ability of the Linux kernel to share a single physical RAM copy of `.text` across processes!
- Passing **`-fPIC`** (documented in [GCC Code Gen Options](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)) instructs the compiler to generate **Position-Independent Code**:
  - **`.text` remains 100% read-only and unmodified**, using PC-relative addressing (`%rip`-relative on `x86_64`, `auipc` on `riscv64`).
  - **Global Offset Table (GOT)**: A per-process private data table in the writable `.got` / `.got.plt` section that stores the actual runtime virtual addresses of global variables and external functions.
  - **Procedure Linkage Table (PLT)**: Small stub trampolines in `.plt` used to call external functions through the `.got.plt` (supporting **lazy binding**, where a function's real address is resolved by `ld.so` only on the very first call).

> **Note on `-fPIC` vs `-fpic`:** Per [GCC Code Gen Options](https://gcc.gnu.org/onlinedocs/gcc-4.3.0//gcc/Code-Gen-Options.html), `-fpic` uses smaller, faster GOT offsets on architectures with fixed instruction widths (such as `aarch64`, `m68k`, or `sparc`), which can fail at link time if the GOT exceeds a size limit (e.g., 28 KB on AArch64). On `x86_64` they are identical, and in general Linux systems programming **`-fPIC` is always preferred** for shared libraries so GOT size limits are never hit.

### The Linux Shared Library Naming & Versioning Triad (`SONAME`)
Linux uses a 3-tier naming convention (detailed in [`ldconfig(8)`](https://man7.org/linux/man-pages/man8/ldconfig.8.html)) so multiple major ABI versions of a library can coexist cleanly while minor bugfix updates happen transparently:

| Name Tier | Example Filename | Who Uses It & How It Is Created |
| :--- | :--- | :--- |
| **1. Real Name** | `libsyswatch.so.1.0.0` | The actual ELF shared object file containing the compiled code (`lib<name>.so.<major>.<minor>.<patch>`). |
| **2. `SONAME`** | `libsyswatch.so.1` | Symbolic link pointing to `libsyswatch.so.1.0.0`. Embedded into the library's ELF header (`DT_SONAME`) via `-Wl,-soname,libsyswatch.so.1`. When an application links against the library, `ld` records **only the `SONAME`** (`libsyswatch.so.1`) in the executable's `DT_NEEDED` tag! Increment `<major>` **only** when you break ABI compatibility. |
| **3. Linker Name** | `libsyswatch.so` | Symbolic link pointing to `libsyswatch.so.1` (or `libsyswatch.so.1.0.0`). Used **strictly at build time** by `ld` when you compile with `-lsyswatch`. |

### Building a Proper Shared Library Step-by-Step
```bash
# 1. Compile each source file to a PIC relocatable object (.o)
gcc -std=c11 -Wall -Wextra -O2 -g -fPIC -Iinclude -c src/log.c -o build/log.o

# 2. Link into a shared object with an embedded SONAME
gcc -shared -Wl,-soname,libsyswatch.so.1 \
    -o build/libsyswatch.so.1.0.0 build/log.o

# 3. Create the SONAME symlink (for runtime ld.so) and Linker Name symlink (for build-time ld)
ln -sf libsyswatch.so.1.0.0 build/libsyswatch.so.1
ln -sf libsyswatch.so.1     build/libsyswatch.so
```

### Production Best Practices: Symbol Visibility & Constructors
1. **Hide Internal Library Symbols (`-fvisibility=hidden`)**:
   By default on ELF systems, every non-`static` C function in a shared library is exported in `.dynsym`, polluting the global symbol namespace and forcing internal calls through the PLT. Per [GCC Code Gen Options](https://gcc.gnu.org/onlinedocs/gcc-4.3.0//gcc/Code-Gen-Options.html), compile shared libraries with `-fvisibility=hidden` and explicitly mark only public API functions in your header with:
   ```c
   #define SW_API __attribute__((visibility("default")))
   ```
2. **Library Constructors & Destructors**:
   Per [`dlopen(3)`](https://man7.org/linux/man-pages/man3/dlopen.3.html), you can run initialization logic automatically when a shared library is loaded (before `main()` or before `dlopen()` returns) and cleanup logic when unloaded using GCC attributes (the legacy `_init` and `_fini` symbols are deprecated):
   ```c
   __attribute__((constructor))
   static void libsyswatch_init(void) {
       /* Runs automatically when libsyswatch.so is mapped into memory */
   }

   __attribute__((destructor))
   static void libsyswatch_fini(void) {
       /* Runs automatically on process exit or dlclose() */
   }
   ```

---

## 3. Linking To Libraries & Inspecting ELF Symbols

When you pass `-L/path/to/lib -lsyswatch` to `gcc`, the linker searches each `-L` directory (followed by system directories) for `libsyswatch.so` first, falling back to `libsyswatch.a` only if no `.so` exists in that directory.

### Mixing Static and Dynamic Libraries in a Single Command
Per [GNU LD Options](https://sourceware.org/binutils/docs-2.29/ld/Options.html), you can switch the linker between static and dynamic selection mid-command using `-Wl,-Bstatic` and `-Wl,-Bdynamic`:
```bash
# Link libsyswatch statically, but link glibc and pthread dynamically:
gcc main.o -Lbuild -Wl,-Bstatic -lsyswatch -Wl,-Bdynamic -lpthread -o syswatchd
```

### Essential ELF & Symbol Inspection Commands
- **`nm` ([GNU `nm` Manual](https://sourceware.org/binutils/docs/binutils/nm.html))**: Lists symbols in `.o`, `.a`, `.so`, or executables (`nm -D` inspects the dynamic symbol table `.dynsym` of stripped shared libraries):
  - `U` = Undefined symbol (must be resolved from another object/library)
  - `T` / `t` = Global (`T`) or local `static` (`t`) function in the `.text` code section
  - `D` / `d` = Initialized global/static variable in `.data`
  - `B` / `b` = Zero-initialized variable in `.bss`
  - `R` / `r` = Read-only constant in `.rodata`
  - `W` / `w` = Weak symbol (can be overridden by a strong symbol of the same name without a multiple-definition linker error)
- **`readelf` ([GNU `readelf` Manual](https://sourceware.org/binutils/docs/binutils/readelf.html))**: The gold-standard ELF parser (independent of the BFD library):
  ```bash
  # Show dynamic section tags: DT_NEEDED, DT_SONAME, DT_RUNPATH
  readelf -d build/libsyswatch.so.1.0.0
  readelf -d build/syswatchd | grep -E '(NEEDED|RUNPATH|RPATH)'
  ```
- **`ldd` ([`ldd(1)`](https://man7.org/linux/man-pages/man1/ldd.1.html))**: Prints the full recursive tree of shared libraries resolved by the dynamic loader:
  ```bash
  ldd build/syswatchd
  ```
  > **Security Rule ([`ldd(1)`](https://man7.org/linux/man-pages/man1/ldd.1.html)):** Never run `ldd` on an untrusted executable! Some implementations invoke the binary's custom ELF `.interp` loader directly, which can execute arbitrary code. To inspect an untrusted binary safely, always use `readelf -d binary | grep NEEDED` or `objdump -p binary | grep NEEDED`.

---

## 4. Dynamic Linking Loader (`ld.so` / `dlopen`)

When you execute `./syswatchd`, the Linux kernel's `execve()` handler reads the ELF `PT_INTERP` program header (viewable via `readelf -l ./syswatchd | grep interpreter`), loads the dynamic linker (`/lib64/ld-linux-x86-64.so.2` or `/lib/ld-linux-riscv64-lp64d.so.1`), and jumps to `ld.so`'s entry point.

### How `ld.so` Locates Shared Libraries at Runtime
If a `DT_NEEDED` string does not contain a slash (`/`), [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html) searches for the file in this exact strict order:

| Priority | Search Location | How to Configure & When to Use |
| :--- | :--- | :--- |
| **1** | **`DT_RPATH`** | Legacy ELF tag embedded in the executable (only searched if `DT_RUNPATH` is **not** present). Overrides `LD_LIBRARY_PATH`, which is why it is deprecated. |
| **2** | **`LD_LIBRARY_PATH`** | Colon-separated environment variable (`LD_LIBRARY_PATH=./build ./syswatchd`). Great for quick local testing; **never** set globally in production `.bashrc` or `/etc/profile`. Ignored automatically in setuid/setgid/capability binaries (`AT_SECURE`). |
| **3** | **`DT_RUNPATH`** | Modern ELF tag embedded at link time via `-Wl,-rpath,/path` (or relative to the executable's own directory using `-Wl,-rpath,'$ORIGIN/../lib'`, per [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html)). |
| **4** | **`/etc/ld.so.cache`** | Binary hash cache compiled by running `sudo ldconfig` ([`ldconfig(8)`](https://man7.org/linux/man-pages/man8/ldconfig.8.html)), which scans directories listed in `/etc/ld.so.conf` and `/etc/ld.so.conf.d/*.conf`. View contents with `ldconfig -p`. |
| **5** | **Trusted System Dirs** | `/lib64` and `/usr/lib64` (or `/lib` and `/usr/lib`). |

### Powerful `ld.so` Environment Variables for Debugging & Hooking
As documented in [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html):
- **`LD_DEBUG`**: Traces the dynamic loader in real time without recompiling:
  - `LD_DEBUG=libs ./syswatchd` — shows every directory searched to find `.so` files.
  - `LD_DEBUG=bindings ./syswatchd` — shows every symbol resolved to its target DSO.
  - `LD_DEBUG=statistics ./syswatchd` — prints relocation timing and cache hit statistics.
  - `LD_DEBUG=help ./syswatchd` — lists all supported `LD_DEBUG` categories.
- **`LD_BIND_NOW=1`** (or linker flag `-Wl,-z,now`): Forces `ld.so` to resolve **all** PLT symbols eagerly at startup before `main()` runs (essential for real-time daemons that cannot tolerate lazy-binding latency spikes or that drop privileges/enter `chroot`/`seccomp` after startup).
- **`LD_PRELOAD`**: Loads specified shared objects **before** all other libraries (including `libc.so.6`), allowing you to intercept (interpose) any standard library call (`malloc`, `open`, `connect`) for debugging, fault injection, or profiling.

### Runtime Plugin Loading API: `dlopen()`, `dlsym()`, `dlerror()`, `dlclose()`
Instead of linking against a shared library at build time, a daemon can load plugins on demand at runtime using `<dlfcn.h>` (documented in [`dlopen(3)`](https://man7.org/linux/man-pages/man3/dlopen.3.html), [`dlsym(3)`](https://man7.org/linux/man-pages/man3/dlsym.3.html), and [`dlerror(3)`](https://man7.org/linux/man-pages/man3/dlerror.3.html)):

```c
#include <dlfcn.h>
#include <stdio.h>

typedef int (*plugin_sample_fn)(void);

int run_plugin(const char *so_path) {
    /* 1. Load the shared object lazily (RTLD_LAZY) or eagerly (RTLD_NOW) */
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return -1;
    }

    /* 2. Clear any prior error state before calling dlsym() (per dlsym(3)) */
    dlerror();

    /* 3. Look up symbol address; use ISO C-safe union/memcpy or POSIX cast */
    plugin_sample_fn sample_fn;
    *(void **)(&sample_fn) = dlsym(handle, "sw_plugin_sample");

    const char *err = dlerror();
    if (err != NULL) {
        fprintf(stderr, "dlsym failed: %s\n", err);
        dlclose(handle);
        return -1;
    }

    /* 4. Invoke the dynamically loaded function and unload when done */
    int rc = sample_fn();
    dlclose(handle);
    return rc;
}
```

---

## 5. Chapter 2 Hands-On Labs & `syswatch` Project Milestone 2

### Part A: Warm-Up Lab — `LD_DEBUG` & Function Interposition with `LD_PRELOAD`
Try this 2-minute experiment in your container to see how tools like `valgrind`, `faketime`, and `tsocks` hijack `glibc` calls using [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html) and `RTLD_NEXT` ([`dlsym(3)`](https://man7.org/linux/man-pages/man3/dlsym.3.html)):

```bash
cat << 'EOF' > hook_puts.c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

int puts(const char *s) {
    int (*real_puts)(const char *) = dlsym(RTLD_NEXT, "puts");
    real_puts("[LD_PRELOAD INTERCEPTED]");
    return real_puts(s);
}
EOF

gcc -shared -fPIC -Wall -Wextra -O2 hook_puts.c -o libhook.so -ldl
LD_PRELOAD=./libhook.so ls --version
```

---

### Part B: Cumulative Mini-Project Milestone 2 — `libsyswatch` (`.a` & `.so.1`) + Plugin Loader

In Milestone 1, you created `include/syswatch.h` and `src/log.c`. Now let's architect **`syswatch`** like a real Linux system package with a shared core library (`libsyswatch.so.1`), a static archive (`libsyswatch.a`), two executables (`syswatchd` and `syswatchctl`), and a dynamic plugin loader in `syswatchd`!

#### Target Project Layout
```text
syswatch/
├── include/
│   ├── syswatch.h          # Public API with SW_API visibility macro
│   └── syswatch_plugin.h   # Plugin contract struct & entry symbol
├── src/
│   ├── log.c               # Core logging + constructor/destructor banner
│   ├── error.c             # Thread-safe strerror_r() helper wrapper
│   ├── syswatchd.c         # Daemon skeleton + dlopen() plugin loader
│   └── syswatchctl.c       # CLI client skeleton linking to libsyswatch.so.1
├── plugins/
│   └── plugin_dummy_cpu.c  # Standalone .so plugin loaded at runtime via dlopen()
└── build/                  # Output directory for .o, .a, .so, and binaries
    ├── lib/
    └── bin/
```

#### Your Implementation Tasks
1. **Symbol Visibility in `include/syswatch.h`**:
   - Define `#define SW_API __attribute__((visibility("default")))` and annotate your public functions (`sw_log_internal`, `sw_strerror`) with `SW_API` so you can compile `libsyswatch.so.1.0.0` with `-fvisibility=hidden`.
2. **Add `src/error.c` (`sw_strerror`)**:
   - Add a helper function `SW_API const char *sw_strerror(int errnum, char *buf, size_t buflen)` that wraps `strerror_r()` and returns a human-readable error string so both `syswatchd` and `syswatchctl` can log system call errors cleanly.
3. **Build Both `libsyswatch.a` and `libsyswatch.so.1.0.0`**:
   - Compile `src/log.c` and `src/error.c` with `-fPIC -fvisibility=hidden`.
   - Create `build/lib/libsyswatch.a` using `ar rcs` and inspect its symbol table with `nm -s build/lib/libsyswatch.a`.
   - Create `build/lib/libsyswatch.so.1.0.0` with `-shared -Wl,-soname,libsyswatch.so.1`, create the `libsyswatch.so.1` and `libsyswatch.so` symlinks, and verify using `readelf -d` and `nm -D --defined-only` that only `SW_API` functions are exported!
4. **Link `syswatchd` and `syswatchctl` Using `$ORIGIN` `RUNPATH`**:
   - Link `build/bin/syswatchd` and `build/bin/syswatchctl` against `-Lbuild/lib -lsyswatch` and embed `-Wl,-rpath,'$ORIGIN/../lib'` so both binaries find `libsyswatch.so.1` automatically without needing `LD_LIBRARY_PATH`!
   - Verify with `ldd build/bin/syswatchd` and `readelf -d build/bin/syswatchd`.
5. **Implement a Runtime Metric Plugin in `plugins/plugin_dummy_cpu.c` and Load It in `syswatchd`**:
   - Define a simple plugin interface in `include/syswatch_plugin.h`:
     ```c
     struct sw_metric_plugin {
         const char *name;
         int (*init)(void);
         int (*collect)(void);
     };
     ```
   - Build `build/lib/plugin_dummy_cpu.so` (`-shared -fPIC`) exporting `struct sw_metric_plugin sw_plugin_entry`.
   - In `src/syswatchd.c`, take a plugin path from `argv[1]` (or default to `./build/lib/plugin_dummy_cpu.so`), load it with `dlopen(..., RTLD_NOW | RTLD_LOCAL)`, look up `"sw_plugin_entry"` with `dlsym()`, call `init()` and `collect()`, log the results via `SW_LOG_INFO(...)`, and close the handle with `dlclose()`.
