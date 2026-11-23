# LFD401 Chapter 7: Memory Management and Allocation — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Memory Management and Allocation** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), examining Linux virtual memory, demand paging and overcommit, `glibc` dynamic allocation (`ptmalloc2`, `brk`, `mmap`, `posix_memalign`), tuning `malloc()` via `mallopt()` and `mallinfo2()`, and locking pages in RAM with `mlock()` / `mlockall()`, followed by **Milestone 7** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. Linux Memory Management Architecture

### A. Virtual Address Space Layout (Per Process)
Every Linux process has its own isolated virtual address space (`mm_struct` in the kernel), divided into **Virtual Memory Areas (VMAs)** viewable via `/proc/<pid>/maps` and `/proc/<pid>/smaps`:

```text
High Virtual Addresses (e.g., 0x7fffffffffff on x86_64)
+---------------------------------------------------------+
| Kernel Space (mapped in upper half, inaccessible Ring 3)|
+---------------------------------------------------------+
| Stack (grows downward; auto-expanded via page faults)   |
|   ↓                                                     |
| ... unmapped gap ...                                    |
|   ↑                                                     |
| Memory-Mapped Segment (mmap): .so libs, anonymous mmaps,|
|   thread stacks, POSIX shared memory, vDSO              |
| ... unmapped gap ...                                    |
|   ↑                                                     |
| Heap (grows upward via brk(2) / sbrk(2) program break)  |
+---------------------------------------------------------+
| BSS (.bss)  — zero-initialized globals/statics          |
| Data (.data) — initialized globals/statics              |
| Text (.text) + Read-Only Data (.rodata)                 |
+---------------------------------------------------------+
Low Virtual Addresses
```

### B. Pages, Demand Paging, Minor vs. Major Faults, and Overcommit
- **Page Size**: Linux manages physical RAM in fixed-size frames (typically **4 KiB** on `x86_64` and `riscv64`; query portably at runtime with `sysconf(_SC_PAGESIZE)`—never hardcode `4096`!).
- **Demand Paging (Lazy Allocation)**: When you call `malloc(100 * 1024 * 1024)`, the kernel does **not** immediately assign 100 MB of physical RAM frames. It merely records a VMA in your `mm_struct` and marks the page-table entries (PTEs) as *not present*.
  - The first time your code reads or writes a byte in a page, the CPU's MMU triggers a **Page Fault**:
    - **Minor Page Fault**: Resolved purely in RAM (allocating a zeroed physical page or mapping an already-cached page—no disk I/O).
    - **Major Page Fault**: Requires reading the page back from disk (swap space or a file-backed `mmap`).
- **Memory Overcommit & the OOM Killer**:
  Because of demand paging, Linux defaults to optimistic memory overcommit (`/proc/sys/vm/overcommit_memory = 0`), meaning **`malloc()` can return a non-`NULL` pointer even when physical RAM is nearly exhausted**. If the system later runs out of physical RAM + swap when pages are touched, the kernel's **OOM Killer** terminates the highest-scoring process with `SIGKILL` (un-catchable!).

---

## 2. Dynamic Memory Allocation (`glibc` `ptmalloc2`)

### A. How `malloc()` Obtains Memory from the Kernel
`glibc`'s allocator (`ptmalloc2`) sits in user space and manages free lists (**tcache**, **fastbins**, **smallbins**, **largebins**, **unsorted bin**) across per-thread **arenas** to avoid lock contention. It only asks the kernel for raw memory via two system calls:
1. **`brk(2)` / `sbrk(2)` ([`brk(2)`](https://man7.org/linux/man-pages/man2/brk.2.html))**: Moves the **program break** (the top edge of the main heap) up or down. Used by default for **small/medium allocations** (below `M_MMAP_THRESHOLD`, initially 128 KiB, then dynamically adjusted).
   - *Rule ([`brk(2)`](https://man7.org/linux/man-pages/man2/brk.2.html))*: Never call `brk()`/`sbrk()` directly in an application that uses `malloc()`—you will corrupt `glibc`'s heap metadata.
2. **`mmap(2)` (`MAP_PRIVATE | MAP_ANONYMOUS`) ([`mmap(2)`](https://man7.org/linux/man-pages/man2/mmap.2.html))**: Used for **large allocations** (≥ `M_MMAP_THRESHOLD`) and for secondary thread arenas. When `free()` is called on an `mmap`-backed chunk, `glibc` immediately calls `munmap(2)` and returns the physical pages straight back to the kernel!

### B. Core Allocation APIs & Safety Gotchas
| Function | Signature (`<stdlib.h>`) | Key Behavior & Systems Programming Pitfalls |
| :--- | :--- | :--- |
| **`malloc()`** | `void *malloc(size_t size)` | Returns **uninitialized** memory aligned to `max_align_t` (16 bytes on 64-bit Linux). Per [`malloc(3)`](https://man7.org/linux/man-pages/man3/malloc.3.html), `glibc` rejects sizes `> PTRDIFF_MAX`. |
| **`calloc()`** | `void *calloc(size_t nmemb, size_t size)` | Zero-initializes memory AND checks for `nmemb * size` integer overflow. For large allocations via `mmap()`, `calloc()` is **faster** than `malloc()` + `memset(0)` because kernel anonymous pages are already zero-filled! |
| **`realloc()`** | `void *realloc(void *ptr, size_t size)` | **Never write `ptr = realloc(ptr, new_size);`!** If `realloc()` fails, it returns `NULL` *without* freeing the original `ptr`, leaking the old block. Always assign to a temporary pointer `void *tmp = realloc(ptr, new_size);`. Also avoid calling `realloc(ptr, 0)` (see non-portable caveats in [`malloc(3)`](https://man7.org/linux/man-pages/man3/malloc.3.html)). |
| **`reallocarray()`** | `void *reallocarray(void *ptr, size_t nmemb, size_t size)` | Overflow-safe array resizing (returns `NULL` with `ENOMEM` if `nmemb * size` overflows, per [`malloc(3)`](https://man7.org/linux/man-pages/man3/malloc.3.html)). |
| **`posix_memalign()`** | `int posix_memalign(void **memptr, size_t align, size_t size)` | Allocates memory aligned to a power-of-two `align` that is a multiple of `sizeof(void *)`—essential for **page-aligned (`4096`) buffers**, **`O_DIRECT` disk I/O**, **SIMD**, and **cache-line alignment (`64` bytes)**. Returns `0` or an error code directly (**does not set `errno`**, per [`posix_memalign(3)`](https://man7.org/linux/man-pages/man3/posix_memalign.3.html))! |
| **`alloca()`** | `void *alloca(size_t size)` | Allocates on the **stack frame** (freed automatically on return). Fast, but **dangerous**: silently overflows the stack (`SIGSEGV`) with no `NULL` error return—avoid for untrusted or large sizes. |

> **Important `errno` Note:** Per [`malloc(3)`](https://man7.org/linux/man-pages/man3/malloc.3.html), `free()` preserves `errno` starting in `glibc 2.33`, so cleanup paths calling `free()` after a failed syscall won't clobber `errno` on modern Linux.

---

## 3. Tuning and Inspecting `malloc()`

### A. Runtime Tuning with `mallopt(3)` & Environment Variables
As documented in [`mallopt(3)`](https://man7.org/linux/man-pages/man3/mallopt.3.html) (`#include <malloc.h>`), `mallopt(int param, int value)` returns **`1` on success and `0` on error** (the opposite of standard POSIX `0`/`-1`, and it does *not* set `errno`!).

| `mallopt()` Parameter | Equivalent Env Var / Tunable | What It Controls & When Daemons Tune It |
| :--- | :--- | :--- |
| **`M_MMAP_THRESHOLD`** | `MALLOC_MMAP_THRESHOLD_` | Byte size above which `malloc()` uses `mmap()` instead of `brk()`. Setting it explicitly disables `glibc`'s dynamic threshold scaling. |
| **`M_TRIM_THRESHOLD`** | `MALLOC_TRIM_THRESHOLD_` | Free space at the top of the `brk()` heap before `free()` calls `sbrk(-n)` to shrink the heap. Real-time/low-latency daemons often set this to `-1` (disable trimming) to eliminate `brk()` syscalls on hot paths. |
| **`M_TOP_PAD`** | `MALLOC_TOP_PAD_` | Extra padding bytes requested when extending the `brk()` heap, reducing syscall frequency. |
| **`M_ARENA_MAX`** | `GLIBC_TUNABLES=glibc.malloc.arena_max=N` | Caps the number of per-thread arenas (default `8 * CPU_cores` on 64-bit). Limiting arenas drastically cuts virtual memory bloat in multi-threaded daemons. |
| **`M_PERTURB`** | `MALLOC_PERTURB_=N` | **Killer debugging trick**: Setting `M_PERTURB` to a nonzero byte `p` (e.g., `0x5A`) fills newly `malloc()`-ed memory with `~p` (`0xA5`) and overwrites `free()`-ed memory with `p` (`0x5A`), immediately exposing uninitialized reads and use-after-free bugs! |
| **`M_CHECK_ACTION`** | `MALLOC_CHECK_` | Controls how `glibc` reacts to heap corruption (`0`=ignore, `1`=print, `2`=abort, `3`=print detailed error + backtrace + abort, per [`mallopt(3)`](https://man7.org/linux/man-pages/man3/mallopt.3.html)). |

### B. Inspecting Heap Statistics (`mallinfo2`, `malloc_stats`, `malloc_trim`)
- **`mallinfo2()` ([`mallinfo(3)`](https://man7.org/linux/man-pages/man3/mallinfo.3.html))**: Replaces the legacy `mallinfo()` (whose 32-bit `int` fields wrapped around above 2 GiB!). Returns `struct mallinfo2` with `size_t` fields:
  - `arena`: Total `brk()` heap bytes allocated from the OS.
  - `uordblks`: Total bytes currently in use by allocations.
  - `fordblks`: Total bytes sitting in `glibc`'s free lists.
  - `hblks` / `hblkhd`: Number and total bytes of active `mmap()`-backed allocations.
  - `keepcost`: Releasable free bytes at the top of the heap.
- **`malloc_trim(0)`**: Manually forces `glibc` to release free pages at the top of the heap back to the Linux kernel via `sbrk()` / `madvise(MADV_DONTNEED)`.
- **`malloc_stats()`**: Prints a formatted per-arena summary directly to `stderr`.

---

## 4. Locking Pages in RAM (`mlock`, `mlockall`, `madvise`)

Normally, the Linux kernel is free to swap any anonymous user-space page out to disk under memory pressure. Two classes of Linux applications **cannot** tolerate swapping:
1. **Security-Sensitive Daemons (SSH, TLS/OpenSSL, Key Managers)**: Prevents cryptographic keys, passwords, or tokens from ever being written in plaintext to a swap partition or swap file.
2. **Real-Time / Low-Latency Daemons**: Eliminates unpredictable millisecond-scale major page faults on critical hot paths.

### A. The Page-Locking API (`<sys/mman.h>`, [`mlock(2)`](https://man7.org/linux/man-pages/man2/mlock.2.html))
```c
int mlock(const void *addr, size_t len);
int mlock2(const void *addr, size_t len, unsigned int flags);   /* MLOCK_ONFAULT */
int munlock(const void *addr, size_t len);
int mlockall(int flags);                                        /* MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT */
int munlockall(void);
```
- **Page Granularity**: Linux locks entire pages containing the `[addr, addr + len)` range. `mlock()` immediately faults in all pages in the range so they are physically resident in RAM when it returns.
- **`mlockall(MCL_CURRENT | MCL_FUTURE)`**: Locks all currently mapped pages **and** automatically locks every future `malloc()`, stack growth, or `mmap()` mapping. Adding `MCL_ONFAULT` (Linux 4.4+, per [`mlock(2)`](https://man7.org/linux/man-pages/man2/mlock.2.html)) defers locking until a page is actually faulted in.
- **Permissions & `RLIMIT_MEMLOCK` ([`mlock(2)`](https://man7.org/linux/man-pages/man2/mlock.2.html))**:
  - Privileged processes with `CAP_IPC_LOCK` can lock unlimited RAM.
  - Unprivileged processes can lock up to their `RLIMIT_MEMLOCK` soft limit (`ulimit -l`, typically 64 KiB to 8 MiB on Ubuntu). Exceeding it fails with **`ENOMEM`**!
- **Not Inherited Across `fork()`**: Memory locks are **not** inherited by child processes created with `fork()` (or across `execve()`), and calling `munlock()` unlocks the whole page even if you previously called `mlock()` twice on it (locks do not stack).
- **Wiping Secrets Before Unlocking**: Always scrub sensitive buffers using **`explicit_bzero(buf, len)`** (which the compiler cannot optimize away as a dead store) *before* calling `munlock()` and `free()`.

### B. Advising the Kernel with `madvise(2)`
Per [`madvise(2)`](https://man7.org/linux/man-pages/man2/madvise.2.html), you can also hint memory-access patterns on page-aligned ranges:
- `MADV_DONTNEED`: Immediately discards anonymous pages and frees physical RAM (future reads return zero-filled pages).
- `MADV_DONTDUMP`: Excludes a sensitive memory region (like an encryption key buffer) from **core dumps** (`ET_CORE`)!
- `MADV_WILLNEED` / `MADV_SEQUENTIAL` / `MADV_RANDOM`: Tunes kernel page-cache readahead for large `mmap()`-ed files.

---

## 5. Chapter 7 Hands-On Labs & `syswatch` Project Milestone 7

### Part A: Warm-Up Lab — Seeing `brk()` vs. `mmap()` and `MALLOC_PERTURB_` in Action
Run this 1-minute experiment in `/workdir`:
```bash
cat << 'EOF' > mem_lab.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *small = malloc(1024);          /* Uses brk() heap */
    char *large = malloc(256 * 1024);    /* Uses mmap() (>= 128 KiB) */
    printf("first byte of uninit small = 0x%02x\n", (unsigned char)small[0]);
    memset(large, 0x42, 256 * 1024);
    free(large);
    free(small);
    return 0;
}
EOF

gcc -std=c11 -Wall -Wextra -O2 mem_lab.c -o mem_lab
strace -e trace=brk,mmap,munmap ./mem_lab
MALLOC_PERTURB_=90 ./mem_lab   # 90 = 0x5A -> newly allocated bytes become ~0x5A = 0xA5!
```

---

### Part B: Cumulative Mini-Project Milestone 7 — Page-Aligned, `mlock()`-Capable Telemetry Ring Buffer & Heap Diagnostics

Now let's give `syswatchd` a real, fixed-memory, zero-fragmentation **telemetry ring buffer** that stores timestamped plugin samples in page-aligned RAM, attempts to lock its pages against swapping (`mlock`), excludes sensitive scratch memory from core dumps (`MADV_DONTDUMP`), and reports live `mallinfo2()` heap metrics.

#### Target Files to Add/Update in `/workdir/syswatch`
```text
syswatch/
├── include/
│   ├── syswatch.h           # Add sw_log_heap_stats()
│   └── syswatch_ringbuf.h   # Public ring-buffer API (SW_API)
└── src/
    ├── ringbuf.c            # Page-aligned ring buffer with mlock() + explicit_bzero()
    └── syswatchd.c          # Allocate ringbuf, record plugin samples, log heap stats
```

#### Your Implementation Tasks
1. **Create `include/syswatch_ringbuf.h` & `src/ringbuf.c`**:
   - Define a sample struct:
     ```c
     struct sw_sample {
         uint64_t timestamp_ns;
         int      value;
         char     source[32];
     };
     ```
   - Implement an opaque `struct sw_ringbuf`:
     - `SW_API struct sw_ringbuf *sw_ringbuf_create(size_t capacity, bool lock_pages);`
       - Calculate byte size rounded up to a multiple of `sysconf(_SC_PAGESIZE)` and allocate the backing array using **`posix_memalign()`** ([`posix_memalign(3)`](https://man7.org/linux/man-pages/man3/posix_memalign.3.html)) with alignment = `page_size` (remember: `posix_memalign()` returns the error code directly, not via `errno`!).
       - Zero-initialize the backing buffer.
       - If `lock_pages` is `true`, call **`mlock(buf, alloc_bytes)`** ([`mlock(2)`](https://man7.org/linux/man-pages/man2/mlock.2.html)). If `mlock()` fails with `ENOMEM` or `EPERM` (e.g., low `RLIMIT_MEMLOCK` in an unprivileged container), log a `SW_LOG_WARN` with `sw_strerror()` and continue gracefully without crashing!
     - `SW_API void sw_ringbuf_push(struct sw_ringbuf *rb, const struct sw_sample *sample);` (overwrites oldest entry when full, advancing head/count).
     - `SW_API size_t sw_ringbuf_count(const struct sw_ringbuf *rb);`
     - `SW_API int sw_ringbuf_get(const struct sw_ringbuf *rb, size_t index, struct sw_sample *out);`
     - `SW_API void sw_ringbuf_destroy(struct sw_ringbuf *rb);`
       - Scrub the backing buffer with **`explicit_bzero()`**, call **`munlock()`** if locked, and `free()` both the backing buffer and the struct.
2. **Add `sw_log_heap_stats()` to `libsyswatch`**:
   - Call **`mallinfo2()`** ([`mallinfo(3)`](https://man7.org/linux/man-pages/man3/mallinfo.3.html)) and log `arena`, `uordblks` (in-use), `fordblks` (free list), and `hblkhd` (`mmap` bytes) via `SW_LOG_INFO(...)`.
3. **Integrate into `src/syswatchd.c` & `Makefile`**:
   - Add `src/ringbuf.c` to `LIB_SRCS` in your `Makefile`.
   - At `syswatchd` startup, create a ring buffer (e.g., capacity `64`, `lock_pages = true`), call the loaded plugin's `collect()` 5 times, push each timestamped `sw_sample` (using `sw_now_ns()`) into the ring buffer, read and log the stored samples, call `sw_log_heap_stats()`, and destroy the ring buffer cleanly before exit.
4. **Verify Under Valgrind, ASan & `MALLOC_PERTURB_`**:
   - Run `make -j$(nproc) all check valgrind-check asan-check`.
   - Run `MALLOC_PERTURB_=90 ./build/bin/syswatchd ./build/lib/plugin_dummy_cpu.so` to confirm zero uninitialized heap reads!
