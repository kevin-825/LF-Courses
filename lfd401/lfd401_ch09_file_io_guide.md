# LFD401 Chapter 9: File I/O — Deep-Dive Study Guide & Lab Manual

This study guide covers the **File I/O** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), examining POSIX file descriptors, `open()`/`openat()` flags and durability (`fsync`/`fdatasync`), `read()`/`write()`/`lseek()` semantics, atomic positional (`pread`/`pwrite`) and scatter-gather vector (`readv`/`writev`) I/O, the `stdio` buffering layer, and 32-bit Large File Support (LFS), followed by **Milestone 9** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. UNIX File I/O & Kernel Data Structures

All UNIX low-level I/O operates on **file descriptors (FDs)**—small non-negative integers (`0` = `STDIN_FILENO`, `1` = `STDOUT_FILENO`, `2` = `STDERR_FILENO`).

### The 3-Table Kernel Relationship
Understanding the difference between a **file descriptor**, an **open file description**, and an **inode** prevents subtle bugs across `dup()`, `fork()`, and multi-threading:
1. **Per-Process File Descriptor Table**: Maps integer FD `3` → pointer to an Open File Description, plus per-FD flags (notably **`FD_CLOEXEC`**).
2. **System-Wide Open File Description (`struct file`)**: Created by each `open()` call. Holds the **current file offset (`f_pos`)**, file status flags (`O_RDONLY`, `O_APPEND`, `O_NONBLOCK`), and a pointer to the VFS inode.
   - Calling `dup()` or `fork()` creates a new FD pointing to the **same** `struct file` (sharing `f_pos`!).
   - Calling `open()` twice on the same filename creates **two independent** `struct file` entries with independent offsets.
3. **In-Memory Inode (`struct inode`)**: One per physical file; holds file metadata, size, permissions, and page-cache pages.

---

## 2. Opening and Closing (`open`, `openat`, `close`, `fsync`)

As documented in [`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html), `glibc` wraps `open()` directly onto the modern `openat(AT_FDCWD, ...)` system call.

```c
#include <fcntl.h>
int open(const char *pathname, int flags, ... /* mode_t mode */);
int openat(int dirfd, const char *pathname, int flags, ... /* mode_t mode */);
```
> **Mandatory Rule:** Whenever `O_CREAT` or `O_TMPFILE` is in `flags`, you **must** supply the 3rd `mode` argument (e.g., `0640` or `0644`), which is masked by `~umask`. Omitting `mode` reads garbage from the stack/register and creates files with random permissions!

### Essential `open(2)` Flags for Linux Systems Code
| Flag | Meaning & Systems Programming Importance |
| :--- | :--- |
| **`O_RDONLY` / `O_WRONLY` / `O_RDWR`** | Access mode (exactly one required). |
| **`O_CREAT` \| `O_EXCL`** | Atomically creates a file **only if it does not already exist** (`EEXIST` otherwise)—prevents TOCTOU race conditions for lock/PID files. |
| **`O_CLOEXEC`** | **Always use in daemons and libraries!** Atomically sets `FD_CLOEXEC` so the FD is closed automatically if another thread calls `execve()`, preventing FD leaks to child processes. |
| **`O_APPEND`** | Guarantees that every `write(2)` atomically seeks to EOF before writing—even when multiple processes append concurrently ([`lseek(2)`](https://man7.org/linux/man-pages/man2/lseek.2.html)). |
| **`O_TRUNC`** | Truncates an existing regular file to 0 bytes on open. |
| **`O_NOFOLLOW`** | Fails with `ELOOP` if the final path component is a symbolic link (prevents symlink attacks in `/tmp`). |
| **`O_SYNC` / `O_DSYNC`** | Makes every `write()` wait until data reaches persistent storage (`O_DSYNC` flushes data + size metadata; `O_SYNC` also flushes timestamps, per [`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html)). |
| **`O_DIRECT`** | Bypasses the kernel page cache (DMA directly between user buffer and block device). Requires aligned buffer address, offset, and length (e.g., via `posix_memalign()`); avoid mixing with buffered I/O or `mmap()` ([`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html)). |
| **`O_TMPFILE`** | Creates an unnamed, unlinked temporary file inside a directory—automatically reclaimed by the kernel when closed even on `SIGKILL`! |

### Closing & Crash-Safe Durability (`fsync` vs. `fdatasync`)
- **`close(fd)`**: Decrements the reference count on `struct file`. Always check `close()`'s return value on writable files (delayed write-back errors on NFS/local disks can surface at `close()` or `fsync()`).
- **Page Cache Reality ([`write(2)`](https://man7.org/linux/man-pages/man2/write.2.html))**: A successful `write()` only copies your bytes into the **kernel page cache in RAM**—it does **not** guarantee they have hit disk!
- **`fsync(fd)` vs. `fdatasync(fd)` ([`fsync(2)`](https://man7.org/linux/man-pages/man2/fsync.2.html))**:
  - `fsync(fd)` flushes dirty file data **and** all inode metadata (including `mtime`/`atime`).
  - `fdatasync(fd)` skips flushing non-essential metadata (like timestamps) unless required to read the data back (such as a file-size change)—fewer disk seeks for append-only journals and databases.
  - **Directory `fsync()` Rule ([`fsync(2)`](https://man7.org/linux/man-pages/man2/fsync.2.html))**: Calling `fsync(file_fd)` after creating or renaming a file does **not** guarantee the new directory entry is on disk! For full power-loss safety, you must also `open(dir, O_RDONLY | O_DIRECTORY)` and `fsync(dir_fd)`.

---

## 3. Reading, Writing, and Seeking (`read`, `write`, `lseek`)

### A. Partial Transfers & Linux's 2 GiB Transfer Cap
- **`read(2)` ([`read(2)`](https://man7.org/linux/man-pages/man2/read.2.html)) & `write(2)` ([`write(2)`](https://man7.org/linux/man-pages/man2/write.2.html))**:
  - `read()` returns `> 0` (bytes read), `0` (EOF), or `-1` (error, check `errno`).
  - **Short reads/writes are NOT errors**: Pipes, sockets, terminals, signals (`EINTR`), or hitting EOF mid-buffer can return fewer bytes than requested. That's why we built `sw_write_all()` in Milestone 6!
  - **Linux Kernel Cap ([`read(2)`](https://man7.org/linux/man-pages/man2/read.2.html))**: On both 32-bit and 64-bit Linux, a single `read()` or `write()` syscall transfers at most **`0x7ffff000` (2,147,479,552 bytes)**.

### B. Seeking & Sparse Files (`lseek(2)`)
As documented in [`lseek(2)`](https://man7.org/linux/man-pages/man2/lseek.2.html), `off_t lseek(int fd, off_t offset, int whence)` repositions `f_pos` using:
- `SEEK_SET`: Absolute byte offset from start of file.
- `SEEK_CUR`: Relative to current offset (`lseek(fd, 0, SEEK_CUR)` queries current position).
- `SEEK_END`: Relative to file size.
- `SEEK_DATA` / `SEEK_HOLE`: Linux extensions for efficiently jumping over unallocated holes in **sparse files**.
- **Sparse Files**: Seeking past EOF and writing 1 byte does **not** allocate disk blocks for the gap in between—the filesystem records a "hole" that reads back as zero bytes (`\0`) while consuming zero physical disk blocks!
- **Non-Seekable FDs**: Calling `lseek()` on a pipe, FIFO, or socket fails with **`ESPIPE`** ([`lseek(2)`](https://man7.org/linux/man-pages/man2/lseek.2.html)).

---

## 4. Positional and Vector I/O (`pread`/`pwrite` & `readv`/`writev`)

### A. Atomic Positional I/O: `pread(2)` and `pwrite(2)`
In multi-threaded daemons where multiple threads read/write the same file descriptor, calling `lseek()` followed by `read()` creates a **race condition** (Thread B can move `f_pos` between Thread A's `lseek()` and `read()`).
- [`pread(2)` / `pwrite(2)`](https://man7.org/linux/man-pages/man2/pread.2.html) perform the seek and I/O in **one atomic kernel operation** and **do not modify the file's shared `f_pos` offset**:
  ```c
  ssize_t pread(int fd, void *buf, size_t count, off_t offset);
  ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
  ```
- **Linux Bug/Quirk ([`pread(2)`](https://man7.org/linux/man-pages/man2/pread.2.html))**: On Linux, if a file was opened with `O_APPEND`, `pwrite()` ignores `offset` and appends to EOF anyway (violating POSIX)—so never combine `O_APPEND` with `pwrite()` when updating a file header at offset `0`!

### B. Scatter-Gather Vector I/O: `readv(2)` and `writev(2)`
When writing structured binary records (e.g., a 16-byte frame header in one `struct` plus a payload buffer in another `struct`), calling `write()` twice incurs two syscalls and risks interleaving with other writers, while `memcpy()`-ing into a temporary buffer wastes CPU and RAM.
- [`readv(2)` / `writev(2)`](https://man7.org/linux/man-pages/man2/readv.2.html) (`#include <sys/uio.h>`) scatter/gather up to `IOV_MAX` (1024 on Linux) non-contiguous buffers in **a single atomic syscall**:
  ```c
  struct iovec iov[2] = {
      { .iov_base = &rec_hdr,  .iov_len = sizeof(rec_hdr) },
      { .iov_base = &sample,   .iov_len = sizeof(sample)  },
  };
  ssize_t n = writev(fd, iov, 2);
  ```
- Linux also provides combined **`preadv()` / `pwritev()`** (positional + vector) and **`preadv2()` / `pwritev2()`** (which accept per-operation flags like `RWF_DSYNC`, `RWF_SYNC`, or `RWF_NOWAIT`, per [`readv(2)`](https://man7.org/linux/man-pages/man2/readv.2.html)).

---

## 5. Standard I/O Library (`stdio`)

The C Standard I/O library ([`stdio(3)`](https://man7.org/linux/man-pages/man3/stdio.3.html), `<stdio.h>`) wraps raw file descriptors inside a user-space **`FILE *`** stream with an internal buffer (typically 4 KiB or 8 KiB) to minimize expensive syscall transitions.

### The 3 Buffering Modes (`setvbuf`)
| Mode Constant | Default For | When User-Space Buffer Flushes to Kernel `write()` |
| :--- | :--- | :--- |
| **`_IOFBF` (Fully Buffered)** | Regular disk files, redirected `stdout` (`./app > out.log`) | Only when the buffer fills completely, `fflush()` is called, or `fclose()` runs. |
| **`_IOLBF` (Line Buffered)** | `stdout` when connected to a terminal (TTY) | On every `\n`, when full, or on `fflush()`. |
| **`_IONBF` (Unbuffered)** | `stderr` | Immediately on every `fprintf()` / `fwrite()` call. |

### Interoperability & Pitfalls (`fopen`, `fdopen`, `fileno`)
- **Linux `fopen(3)` Extensions ([`fopen(3)`](https://man7.org/linux/man-pages/man3/fopen.3.html))**: Append **`"e"`** (e.g., `fopen(path, "re")`) to atomically set `O_CLOEXEC`, and **`"x"`** (`"wx"`) for `O_EXCL`.
- **Bridging FDs and `FILE *`**: Use `fdopen(fd, "r")` to wrap an existing FD in a `FILE *` (note: `fclose()` will also close `fd`!) and `fileno(fp)` to retrieve the underlying FD.
- **Never Mix `FILE *` and Raw `read()`/`write()` Without `fflush()`**: Because `FILE *` maintains its own user-space buffer offset ahead of or behind the kernel's `f_pos`, mixing `fprintf(fp, ...)` and `write(fileno(fp), ...)` scrambles output unless you `fflush(fp)` first!
- **The `fork()` Double-Flush Trap**: Always call `fflush(NULL)` before `fork()`—otherwise both parent and child inherit the same unflushed user-space `stdio` buffer and flush duplicate lines on exit!

---

## 6. Large File Support (LFS — 32-bit Systems Only)

On 64-bit Linux (`x86_64`, `riscv64`, `aarch64`), `off_t` and `ino_t` are natively **64-bit** (`sizeof(off_t) == 8`), supporting files up to 8 EiB out of the box.

On **32-bit Linux** (such as 32-bit ARM `armv7l` or `i386` embedded targets):
- Historically, `off_t` was a signed 32-bit `long`, capping file sizes at **`(1<<31) - 1` (2 GiB - 1 byte)**! Opening a file larger than 2 GiB fails with **`EOVERFLOW`** ([`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html)).
- **The Portable LFS Fix**: Compile with **`-D_FILE_OFFSET_BITS=64`** (before any `#include`). This transparently remaps `off_t` to 64-bit `off64_t` and redirects `open()`, `lseek()`, `pread()`, `stat()`, and `fseeko()` to their 64-bit kernel counterparts (`O_LARGEFILE`, `lseek64`, `stat64`) without changing a single line of C code!
- **Portability Rule**: Never cast `off_t` to `int` or `long`—always print `off_t` values as `(intmax_t)off` with `%jd` (or `(long long)off` with `%lld`) and use `fseeko()`/`ftello()` instead of legacy `fseek()`/`ftell()` (which take `long`).

---

## 7. Chapter 9 Hands-On Labs & `syswatch` Project Milestone 9

### Part A: Warm-Up Lab — Sparse Files & `stdio` Redirection Buffering
1. **Create a 100 MiB Sparse File Using 0 Bytes of Disk**:
   ```bash
   python3 -c 'import os; fd=os.open("/tmp/sparse.bin", os.O_CREAT|os.O_WRONLY, 0o644); os.lseek(fd, 100*1024*1024, os.SEEK_SET); os.write(fd, b"X"); os.close(fd)'
   ls -lh /tmp/sparse.bin   # Apparent size: 101M
   du -h  /tmp/sparse.bin   # Actual disk blocks allocated: 4.0K!
   rm -f  /tmp/sparse.bin
   ```

---

### Part B: Cumulative Mini-Project Milestone 9 — Persistent Binary Telemetry Journal (`writev` + `pread`/`pwrite` + `fdatasync`)

Right now, `syswatchd`'s `sw_ringbuf` lives purely in RAM—when `syswatchd` exits, all collected metrics vanish. Let's add a **crash-safe binary telemetry journal** to `libsyswatch` so `syswatchd` persists samples to disk using **Vector I/O (`writev`)** and **Positional I/O (`pwrite`/`pread`)**, and **`syswatchctl`** can inspect any record directly from the journal file!

#### Binary Journal File Layout (`build/syswatch.jnl`)
```text
Offset 0:   [ struct sw_jnl_file_hdr (32 bytes): magic "SWJN", version=1, record_count ]
Offset 32:  [ struct sw_jnl_rec_hdr (16 bytes): seq_no, payload_len, crc/flags ] [ struct sw_sample ]
...         [ struct sw_jnl_rec_hdr (16 bytes) ] [ struct sw_sample ]
```

#### Target Files to Add/Update in `/workdir/syswatch`
```text
syswatch/
├── include/
│   └── syswatch_journal.h   # File header, record header, and SW_API journal functions
└── src/
    ├── journal.c            # open(O_CLOEXEC), writev(), pread()/pwrite(), fdatasync()
    ├── syswatchd.c          # Append every collected sample into the binary journal
    └── syswatchctl.c        # Add `dump <journal_path> [index]` subcommand using pread()
```

#### Your Implementation Tasks
1. **Create `include/syswatch_journal.h` & `src/journal.c`**:
   - Define fixed-width structs (`<stdint.h>`):
     ```c
     #define SW_JNL_MAGIC 0x4E4A5753U  /* "SWJN" in little-endian */

     struct sw_jnl_file_hdr {
         uint32_t magic;
         uint32_t version;
         uint64_t record_count;
         uint64_t reserved[2];
     };

     struct sw_jnl_rec_hdr {
         uint64_t seq_no;
         uint32_t payload_len;
         uint32_t flags;
     };
     ```
   - Implement:
     - `SW_API int sw_jnl_open(const char *path, bool create);`
       - Uses `open(path, O_RDWR | (create ? O_CREAT : 0) | O_CLOEXEC, 0640)` (**do NOT use `O_APPEND`**, because we update offset `0` with `pwrite()`!).
       - Checks file size via `lseek(fd, 0, SEEK_END)`. If `0` (new file), writes an initialized `sw_jnl_file_hdr` at offset `0` with `pwrite()` and calls `fdatasync(fd)`. If non-zero, validates `magic == SW_JNL_MAGIC` using `pread(fd, &hdr, sizeof(hdr), 0)`.
     - `SW_API int sw_jnl_append(int fd, const struct sw_sample *sample);`
       - Reads current `record_count` from offset `0` via **`pread(2)`**.
       - Seeks to `SEEK_END` and writes `sw_jnl_rec_hdr` + `*sample` in **one single `writev(2)` syscall** using a 2-element `struct iovec iov[2]` (handling `EINTR` / short writes cleanly).
       - Increments `hdr.record_count` and writes the updated header back to offset `0` via **`pwrite(2)`**, then flushes to disk with **`fdatasync(2)`**.
     - `SW_API int sw_jnl_read_record(int fd, uint64_t index, struct sw_jnl_rec_hdr *rec_hdr, struct sw_sample *out_sample);`
       - Computes exact byte offset `sizeof(struct sw_jnl_file_hdr) + index * (sizeof(struct sw_jnl_rec_hdr) + sizeof(struct sw_sample))` and reads both structs in **one single `preadv(2)` syscall** (zero `lseek()` calls!).
2. **Integrate into `syswatchd.c`, `syswatchctl.c`, and `Makefile`**:
   - Add `src/journal.c` to `LIB_SRCS` in your `Makefile`.
   - In `syswatchd.c`, open `/tmp/syswatch.jnl`, append each of the 5 collected plugin samples via `sw_jnl_append()`, and `close()` cleanly.
   - In `syswatchctl.c`, support `./build/bin/syswatchctl dump /tmp/syswatch.jnl` (reads header via `pread()`, loops `0 .. record_count - 1` using `sw_jnl_read_record()`, and prints a formatted table of `seq_no`, `timestamp_ns`, `source`, and `value`).
3. **Verify Under `strace`, `valgrind`, and `ASan`**:
   - Run `make -j$(nproc) all valgrind-check asan-check`.
   - Run `strace -e trace=openat,pread64,pwrite64,writev,fdatasync ./build/bin/syswatchd ./build/lib/plugin_disk_usage.so` and watch your `writev` + `pwrite64` + `fdatasync` calls hit the kernel!
   - Run `./build/bin/syswatchctl dump /tmp/syswatch.jnl` to inspect the persisted telemetry records.
