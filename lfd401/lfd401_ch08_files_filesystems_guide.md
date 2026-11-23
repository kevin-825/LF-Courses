# LFD401 Chapter 8: Files and Filesystems in Linux — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Files and Filesystems in Linux** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), exploring Linux's 7 file types and inodes ([`inode(7)`](https://man7.org/linux/man-pages/man7/inode.7.html)), the kernel's Virtual File System ([VFS](https://docs.kernel.org/filesystems/vfs.html)), the evolution from `ext2` to `ext3` journaling (`jbd2`) and modern `ext4` ([`ext4(5)`](https://www.man7.org/linux/man-pages/man5/ext4.5.html)), followed by **Milestone 8** of your **`syswatchd` + `syswatchctl`** project.

---

## 1. Files, Directories, and Devices

In UNIX and Linux, *"everything is a file"* means regular files, directories, hardware devices, pipes, sockets, and kernel runtime interfaces (`/proc`, `/sys`) all share a single unified hierarchical namespace and VFS syscall interface (`open`, `read`, `write`, `close`, `stat`).

### A. The 7 Linux File Types (`inode(7)`)
Every filesystem object is represented by an **inode** (index node) holding its metadata—permissions, owner UID/GID, timestamps (`atime`, `mtime`, `ctime`), link count, size, and data block pointers—but **not its filename**. As documented in [`inode(7)`](https://man7.org/linux/man-pages/man7/inode.7.html), the upper bits of `st_mode` (`st_mode & S_IFMT`) identify one of 7 file types:

| `ls -l` Char | POSIX Macro / Constant | Description & Real-World Linux Example |
| :--- | :--- | :--- |
| **`-`** | `S_ISREG(m)` / `S_IFREG` | Regular byte-stream file (ELF binaries, C source, logs, `/etc/passwd`). |
| **`d`** | `S_ISDIR(m)` / `S_IFDIR` | Directory: a special file mapping human-readable **filenames → inode numbers**. |
| **`l`** | `S_ISLNK(m)` / `S_IFLNK` | Symbolic link: stores a pathname string pointing to another file (can cross filesystems and dangle if target is deleted). |
| **`c`** | `S_ISCHR(m)` / `S_IFCHR` | Character device: unbuffered sequential byte stream (`/dev/null`, `/dev/urandom`, `/dev/ttyS0`). |
| **`b`** | `S_ISBLK(m)` / `S_IFBLK` | Block device: random-access, block-buffered storage (`/dev/nvme0n1`, `/dev/sda`, `/dev/loop0`). |
| **`p`** | `S_ISFIFO(m)` / `S_IFIFO` | Named pipe (FIFO): kernel ring-buffer IPC endpoint created with `mkfifo()`. |
| **`s`** | `S_ISSOCK(m)` / `S_IFSOCK` | UNIX domain socket: bidirectional local IPC endpoint (e.g., `/run/syswatchd.sock`). |

### B. Hard Links vs. Symbolic Links & Unlinked Open Files
- **Hard Link**: Multiple directory entries pointing to the **exact same inode number** (within the same mounted filesystem). The inode's reference counter (`st_nlink`) tracks how many directory entries point to it.
- **Why Deleting an Open Log File Doesn't Free Disk Space**: As explained in [Overview of the Linux Virtual File System](https://docs.kernel.org/filesystems/vfs.html), an open `struct file` keeps its dentry and inode pinned in memory. When you `unlink()` a file, its directory entry disappears and `st_nlink` drops to `0`, **but the kernel will not free the inode or its data blocks until the last open file descriptor referencing it is closed!**
- **Device Major & Minor Numbers**: Character and block device nodes in `/dev` contain no data blocks; instead, their inode stores a `dev_t` (`st_rdev`) split into a **major number** (identifies the kernel driver) and a **minor number** (identifies the specific device instance/partition), inspectable via `major(st.st_rdev)` / `minor(st.st_rdev)` (`<sys/sysmacros.h>`).
- **Special Mode Bits ([`inode(7)`](https://man7.org/linux/man-pages/man7/inode.7.html))**:
  - `S_ISUID` (`04000`): Executes with file owner's effective UID.
  - `S_ISGID` (`02000`): On directories, newly created files inherit the directory's GID.
  - `S_ISVTX` (`01000`, sticky bit): On shared directories like `/tmp`, only the file owner, directory owner, or root can rename or delete a file.
- **Pseudo-File Gotcha ([`inode(7)`](https://man7.org/linux/man-pages/man7/inode.7.html))**: Kernel-generated files in `/proc` often report `st_size == 0` (and `/sys` often reports `4096`) even when they contain text—**never** trust `st_size` to `malloc()` a read buffer for `/proc` files; always read in a loop until EOF (`read() == 0`).

---

## 2. The Virtual File System (VFS)

The **Virtual File System** (Virtual Filesystem Switch, documented in [Overview of the Linux Virtual File System](https://docs.kernel.org/filesystems/vfs.html)) is an object-oriented abstraction layer inside the Linux kernel that lets user-space programs use identical syscalls across `ext4`, `XFS`, `Btrfs`, `NFS`, `tmpfs`, `overlayfs`, and `procfs` (list all registered filesystems via `cat /proc/filesystems`).

### The 4 Core VFS Kernel Objects
| VFS Kernel Struct | Represents | Operations Table (`vtable`) | Key Kernel Role |
| :--- | :--- | :--- | :--- |
| **`struct super_block`** | A mounted filesystem instance | `struct super_operations` (`s_op`) | Tracks block size, dirty inodes, mount flags, and filesystem-wide sync/statfs methods. |
| **`struct inode`** | A unique file/dir/device object | `struct inode_operations` (`i_op`) | Holds metadata, permissions, and namespace methods (`lookup`, `create`, `mkdir`, `unlink`, `rename`). |
| **`struct dentry`** | A directory entry (pathname component) | `struct dentry_operations` (`d_op`) | **Lives purely in RAM** (never written to disk) inside the **dcache** to accelerate pathname-to-inode lookups; caches *negative dentries* (non-existent paths) too! |
| **`struct file`** | An **open file description** | `struct file_operations` (`f_op`) | Allocated on `open()`; stores current byte offset (`f_pos`), access flags (`O_RDONLY`/`O_NONBLOCK`), and I/O methods (`read_iter`, `write_iter`, `mmap`, `ioctl`, `poll`). |

### How a Process File Descriptor Reaches Disk
```text
User Space FD (e.g., fd = 3)
  -> current->files->fdt->fd[3]   (Per-process File Descriptor Table)
    -> struct file                (Open file description: f_pos, f_flags, f_op)
      -> struct dentry            (Cached pathname -> inode link in dcache)
        -> struct inode           (On-disk or pseudo-FS object metadata & i_mapping page cache)
          -> struct super_block   (Mounted ext4/xfs/tmpfs instance)
```
> **Mount Inspection ([`mount(2)`](https://man7.org/linux/man-pages/man2/mount.2.html)):** Each process sees the mounted filesystem hierarchy of its mount namespace via `/proc/self/mounts` and `/proc/self/mountinfo` (which also exposes mount IDs and propagation flags).

---

## 3. The `ext2` / `ext3` Filesystem

Understanding **`ext2`** explains the on-disk foundation that `ext3` and `ext4` still build upon ([ext4 Data Structures and Algorithms](https://docs.kernel.org/filesystems/ext4/)):

### On-Disk Layout: Block Groups
To prevent disk head thrashing and keep related inodes and data blocks physically close together, an `ext2` partition is divided into fixed-size **Block Groups**:
1. **Superblock** (1024 bytes at byte offset 1024 in Group 0, with backup copies in selected groups via `sparse_super`, per [`ext4(5)`](https://www.man7.org/linux/man-pages/man5/ext4.5.html)): Stores total blocks, total inodes, block size (1 KiB / 2 KiB / 4 KiB), UUID, and mount state.
2. **Block Group Descriptor Table (BGDT)**: Locates the bitmaps and inode table for every block group.
3. **Data Block Bitmap & Inode Bitmap**: 1 bit per block/inode (`0` = free, `1` = allocated).
4. **Inode Table**: Fixed-size array of on-disk `struct ext2_inode` entries (created at `mkfs` time—which is why a filesystem can run out of free inodes and return `ENOSPC` even when gigabytes of disk space are still free!).
5. **Data Blocks**: Actual file and directory payloads.

### Classic Indirect Block Mapping (Why `ext2`/`ext3` Struggled with Huge Files)
Each `ext2`/`ext3` inode has 15 block pointers (`i_block[0..14]`):
- `0..11`: **12 Direct blocks** (48 KiB with 4 KiB blocks).
- `12`: **Single indirect block** (points to a block of 1,024 block pointers = 4 MiB).
- `13`: **Double indirect block** (~4 GiB).
- `14`: **Triple indirect block** (~4 TiB).
Deleting or reading a multi-gigabyte file required chasing thousands of scattered indirect metadata blocks!

---

## 4. Journaling Filesystems (`ext3` / `jbd2`)

### The Problem `ext2` Had on Power Loss
Creating a file or appending data requires updating **multiple** on-disk structures (allocating a data block in the bitmap, updating the inode size/pointers, adding a directory entry). If power fails halfway through, `ext2` is left corrupted and forces a slow, full-disk `e2fsck` scan on boot.

### How Write-Ahead Logging (`jbd2`) Solves It
`ext3` (using `jbd`) and `ext4` (using **`jbd2`**—Journal Block Device 2, documented in [ext4 Journal (`jbd2`)](https://docs.kernel.org/filesystems/ext4/journal.html)) add a hidden circular log file (typically **inode 8**) on disk:
1. Metadata changes are grouped into an atomic **transaction** and written sequentially to the journal along with a **Descriptor block** and a **Commit block** (enforced by disk write barriers, [`ext4(5)`](https://www.man7.org/linux/man-pages/man5/ext4.5.html)).
2. Later (default commit interval: 5 seconds, `commit=5`), the kernel checkpoints the committed blocks to their final on-disk locations.
3. On crash recovery, mounting simply replays committed transactions from the journal in **seconds** instead of scanning terabytes of blocks!

### The 3 `ext3`/`ext4` Journaling Modes ([ext4 Journal (`jbd2`)](https://docs.kernel.org/filesystems/ext4/journal.html))
| Mount Option | What Goes Through the Journal? | Crash Guarantee & Performance Trade-off |
| :--- | :--- | :--- |
| **`data=ordered`** *(Default)* | **Metadata only**—but the kernel flushes dirty **data blocks to disk *before*** committing the metadata transaction. | Fast and prevents stale/garbage data from appearing inside newly appended files after a crash. |
| **`data=journal`** | **Both data and metadata** are written to the journal first, then copied to final locations. | Highest consistency guarantee (data is written twice, so noticeably slower for large sequential writes). |
| **`data=writeback`** | **Metadata only**, with **no ordering** between data writes and metadata commits. | Fastest raw write throughput, but after a crash a newly extended file may contain old/uninitialized disk blocks. |

---

## 5. The `ext4` Filesystem

`ext4` is backward-compatible with `ext2`/`ext3` ([`ext4(5)`](https://www.man7.org/linux/man-pages/man5/ext4.5.html)) while redesigning core scaling bottlenecks:
- **Extents (Replacing Indirect Block Lists)**: Instead of listing every 4 KiB block individually, an **extent** describes a contiguous run of blocks as a single triple `(logical_start, length, physical_start)` (up to 128 MiB per single extent!). This dramatically shrinks metadata overhead and speeds up large-file I/O and deletion.
- **Delayed Allocation (`delalloc`)**: Defers assigning physical disk blocks until dirty pages are actually flushed from the page cache, allowing the **Multiblock Allocator (`mballoc`)** to place an entire file in one contiguous extent (minimizing fragmentation).
- **64-Bit Block Numbers & Massive Limits**: Supports up to 1 EiB filesystem size (with 4 KiB blocks and `64bit` feature) and 16 TiB max file size.
- **Nanosecond Timestamps & Extended Inodes**: Increases default inode size from 128 bytes to 256 bytes, adding nanosecond precision (`st_mtim.tv_nsec`), file creation time (`crtime` / `statx(2)`), and inline fast extended attributes (`xattr`).
- **`uninit_bg` & Metadata Checksums**: Lazily initializes unused block groups (making `mkfs.ext4` and `e2fsck` orders of magnitude faster) and protects superblocks, inodes, and `jbd2` transactions with `crc32c` checksums ([`ext4(5)`](https://www.man7.org/linux/man-pages/man5/ext4.5.html)).
- **Fast Commits**: Optional lightweight `jbd2` delta commits that cut `fsync()` latency in `data=ordered` mode ([ext4 Journal (`jbd2`)](https://docs.kernel.org/filesystems/ext4/journal.html)).

---

## 6. Chapter 8 Hands-On Labs & `syswatch` Project Milestone 8

### Part A: Container-Safe `ext4` Disk Image Inspection Lab (`e2fsprogs`)
You don't even need root `mount -o loop` privileges to build and dissect a real `ext4` filesystem! Run this in `/tmp` inside your container:
```bash
# 1. Create a 32 MiB raw disk image and format it as ext4
dd if=/dev/zero of=/tmp/test_ext4.img bs=1M count=32 status=progress
mkfs.ext4 -L SYSWATCH_FS /tmp/test_ext4.img

# 2. Inspect the Superblock, Features (extents, has_journal, flex_bg), and Block Groups
dumpe2fs -h /tmp/test_ext4.img

# 3. Use debugfs to inspect special inode 8 (the hidden jbd2 journal!) and root inode 2
debugfs -R "stat <8>" /tmp/test_ext4.img
debugfs -R "stat <2>" /tmp/test_ext4.img
rm -f /tmp/test_ext4.img
```

---

### Part B: Cumulative Mini-Project Milestone 8 — Real Filesystem Telemetry (`statvfs`) & `plugin_disk_usage.so`

Up to now, `syswatchd` has only loaded `plugin_dummy_cpu.so`. Let's replace the dummy data with your **first real Linux kernel telemetry plugin** using [`statvfs(3)`](https://man7.org/linux/man-pages/man3/statvfs.3.html)!

#### Target Files to Add/Update in `/workdir/syswatch`
```text
syswatch/
├── include/
│   └── syswatch.h              # Add struct sw_fs_stats & sw_get_fs_stats()
├── src/
│   └── fs_stats.c              # statvfs(3) wrapper computing block & inode usage
└── plugins/
    └── plugin_disk_usage.c     # Real metric plugin returning disk usage % (0..100)
```

#### Your Implementation Tasks
1. **Add `struct sw_fs_stats` & `sw_get_fs_stats()` to `libsyswatch` (`include/syswatch.h` + `src/fs_stats.c`)**:
   - Define in `include/syswatch.h`:
     ```c
     struct sw_fs_stats {
         uint64_t total_bytes;
         uint64_t avail_bytes;   /* Available to unprivileged users (f_bavail * f_frsize) */
         uint64_t total_inodes;
         uint64_t free_inodes;
         int      block_used_pct;
         int      inode_used_pct;
         unsigned long mount_flags; /* ST_RDONLY, ST_NOSUID, ST_NOEXEC, etc. */
     };

     SW_API int sw_get_fs_stats(const char *path, struct sw_fs_stats *out);
     ```
   - Implement `sw_get_fs_stats()` in `src/fs_stats.c` using **`statvfs(path, &sv)`** ([`statvfs(3)`](https://man7.org/linux/man-pages/man3/statvfs.3.html)):
     - **Important Gotcha**: Always multiply `sv.f_blocks` and `sv.f_bavail` by **`sv.f_frsize`** (fundamental filesystem block size), *not* `sv.f_bsize` (preferred I/O transfer size), and guard against division by zero if `f_blocks == 0` or `f_files == 0` (common on virtual/pseudo filesystems!).
     - Log a warning if `sv.f_flag & ST_RDONLY` is set (read-only filesystem).
   - Add `src/fs_stats.c` to `LIB_SRCS` in your `Makefile`.
2. **Build Your First Real Plugin: `plugins/plugin_disk_usage.c`**:
   - Export `struct sw_metric_plugin sw_plugin_entry` with `.name = "disk_usage"`.
   - In `collect()`, call `sw_get_fs_stats("/", &stats)` (or check `getenv("SYSWATCH_FS_PATH")` defaulting to `"/"`), log total/available MiB plus inode usage (`free_inodes / total_inodes`), and return `stats.block_used_pct` (`0`–`100`) so `syswatchd` stores real filesystem telemetry into your `mlock()`-ed `sw_ringbuf`!
   - Add `build/lib/plugin_disk_usage.so` to your `Makefile` (linking it against `-Lbuild/lib -lsyswatch`).
3. **Verify in Your Container**:
   - Run `make -j$(nproc) all valgrind-check asan-check`.
   - Test your new plugin live:
     ```bash
     ./build/bin/syswatchd ./build/lib/plugin_disk_usage.so
     SYSWATCH_FS_PATH=/tmp ./build/bin/syswatchd ./build/lib/plugin_disk_usage.so
     ```
   - Compare your plugin's numbers against `df -h /` and `df -i /` to verify your block and inode percentage math!
