# LFD401 Chapter 3: Make — Deep-Dive Study Guide & Lab Manual

This study guide covers the **Make** module of [Developing Applications for Linux (LFD401)](https://training.linuxfoundation.org/training/developing-applications-for-linux-2/), examining how GNU `make` constructs its dependency graph, manages multi-directory C projects, generates automatic header dependencies with GCC, and uses built-in vs. custom pattern rules, culminating in **Milestone 3** of your **`syswatchd` + `syswatchctl`** project.

## 1. Using `make` and `Makefile`s

GNU `make` is a dependency-graph execution engine. Rather than blindly recompiling every `.c` file like a shell script, `make` builds a **Directed Acyclic Graph (DAG)** of targets and prerequisites and uses the `stat()` system call to compare nanosecond file modification timestamps (`mtime`).

### Anatomy of a Rule & the Two-Phase Execution Model

```
target1 target2: prereq1 prereq2 | order_only_prereq
	recipe_command_1
	recipe_command_2

```

> **Hard Syntax Rule:** Every recipe line **must** begin with a literal **TAB character (`\t`, ASCII `0x09`)**, not spaces (unless you explicitly redefine `.RECIPEPREFIX`).

GNU `make` processes a `Makefile` in two distinct phases:

1. **Phase 1 — Read-In Phase (Immediate Evaluation)**:
   `make` reads the `Makefile` top-to-bottom, processes `include` directives, expands `:=` (simple) variables and target/prerequisite names immediately, and constructs the complete internal DAG.

2. **Phase 2 — Target-Update Phase (Deferred Evaluation)**:
   Starting from the default goal (the first target in the `Makefile` whose name does not start with `.`, unless overridden on the CLI or via `.DEFAULT_GOAL`), `make` traverses the DAG depth-first. If a target does not exist or any normal prerequisite has a newer `mtime` than the target, `make` expands the recipe's deferred variables and executes each recipe line in **its own separate `/bin/sh -c` subshell**.

### Variable Assignment Flavors & Modifiers

| Operator | Evaluation Flavor | Behavior & When to Use in Systems `Makefile`s | 
| ----- | ----- | ----- | 
| `VAR := val` | **Simple (Immediate)** | Expanded **once** when parsed during Phase 1 (`::=` is the POSIX standard synonym). **Default choice** in modern `Makefile`s—predictable and fast, especially when calling `$(shell ...)`. | 
| `VAR = val` | **Recursive (Lazy)** | Stored as verbatim text and re-expanded every time `$(VAR)` is referenced. Useful when `VAR` references other variables defined later in the file, but dangerous with `$(shell ...)` (re-runs the shell command on every reference!). | 
| `VAR ?= val` | **Conditional Default** | Assigns `val` only if `VAR` is not already defined (e.g., `CC ?= gcc` or `BUILD ?= debug`). Note: because `make` pre-populates `CC = cc`, test `ifeq ($(origin CC), default)` if you want to override `make`'s built-in `cc` with `gcc`. | 
| `VAR += val` | **Append** | Appends `val` (separated by a space) while preserving `VAR`'s original immediate or recursive flavor. | 
| `override VAR += val` | **CLI Override Protection** | When a user runs `make CFLAGS="-O0"`, command-line assignments override normal `CFLAGS = ...` lines inside the `Makefile`. Prefix critical flags (like `override CFLAGS += -Iinclude -fPIC`) with `override` so user CLI flags cannot accidentally strip required include paths. | 

### Recipe Line Prefixes & `.PHONY` Targets

* **`@` (Silent)**: `@echo "CC $@"` suppresses printing the command itself before executing it.

* **`-` (Ignore Errors)**: `-rm -f *.o` continues even if the command exits with a non-zero status.

* **`+` (Force Under `-n` / Jobserver)**: Executes the recipe line even when invoked with `make -n` (`--dry-run`); automatically implied whenever a recipe references `$(MAKE)`.

* **`.PHONY` Targets ([Phony Targets (GNU make)](https://www.gnu.org/software/make/manual/html_node/Phony-Targets.html))**:
  Targets like `all`, `clean`, `test`, and `install` are action names, not actual output files. Always declare them with `.PHONY`:

  ```
  .PHONY: all clean check install
  
  ```

  Declaring `.PHONY` serves two purposes:

  1. It guarantees `make clean` still runs even if someone accidentally creates a file named `clean` in the directory.

  2. It skips `make`'s built-in implicit rule search for that target, speeding up DAG evaluation.

  > **Trap ([Phony Targets (GNU make)](https://www.gnu.org/software/make/manual/html_node/Phony-Targets.html)):** Never list a `.PHONY` target as a normal prerequisite of a real file target! Because a phony target is always considered out-of-date, any real file depending on it will be rebuilt on every single `make` run.

## 2. Building Large Projects

As a C project grows into separate `include/`, `src/`, `plugins/`, and `build/` directories, manual per-file rules become unmaintainable.

### A. Text & Filename Transformation Functions

As documented in [Text Functions (GNU make)](https://www.gnu.org/software/make/manual/html_node/Text-Functions.html), `make` provides built-in functions to compute object and dependency file lists automatically:

```
# 1. Discover or explicitly list source files
LIB_SRCS := src/log.c src/error.c

# 2. Transform src/foo.c -> build/obj/lib/foo.o using patsubst (or substitution shorthand)
LIB_OBJS := $(patsubst src/%.c,build/obj/lib/%.o,$(LIB_SRCS))
# Equivalent shorthand: LIB_OBJS := $(LIB_SRCS:src/%.c=build/obj/lib/%.o)

# 3. Transform .o -> .d for GCC auto-generated header dependency files
LIB_DEPS := $(LIB_OBJS:.o=.d)

```

### B. Out-of-Tree Build Directories & Order-Only Prerequisites (`|`)

When placing `.o`, `.a`, `.so`, and binaries inside `build/`, the directory must exist before `gcc -o build/obj/log.o` runs.

* **Why normal prerequisites fail for directories**: Every time a new file is written inside `build/obj/`, the Linux VFS updates the directory's `mtime`! If you write `build/obj/%.o: src/%.c build/obj`, `make` sees `build/obj` is newer than existing `.o` files and needlessly recompiles files on the next run.

* **The Solution — Order-Only Prerequisites (`|`)**:
  Prerequisites placed after a pipe symbol (`|`) are **order-only**: `make` ensures they are built before the target if they do not exist, but **ignores their `mtime`** when deciding whether the target is out-of-date:

  ```
  build/obj/lib/%.o: src/%.c | build/obj/lib
  	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
  
  build/obj/lib:
  	mkdir -p $@
  
  ```

### C. Multi-Directory Builds: Recursive `$(MAKE)` vs. `include`

1. **Recursive `make` ([Recursion (GNU make)](https://www.gnu.org/software/make/manual/html_node/Recursion.html))**:
   Invokes sub-`Makefile`s in subdirectories. Always invoke `$(MAKE) -C subdir` (never bare `make -C subdir`) so GNU `make`'s parallel jobserver (`-jN` pipe file descriptors) and flags (`-n`, `-k`) are inherited:

   ```
   SUBDIRS := lib daemon cli
   .PHONY: all $(SUBDIRS)
   all: $(SUBDIRS)
   $(SUBDIRS):
   	$(MAKE) -C $@
   daemon cli: lib   # Enforce parallel build ordering: build lib before daemon & cli!
   
   ```

2. **Non-Recursive `make` with `include`**:
   In modern systems projects, a single top-level `Makefile` (or modular `mk/*.mk` fragments pulled in via `include`) gives `make` a **single unified DAG**, enabling full parallelization (`make -j$(nproc)`) with zero cross-directory race conditions.

## 3. More Complicated Rules

### A. Automatic Variables Reference

Inside a rule's recipe, use [Automatic Variables (GNU make)](https://www.gnu.org/software/make/manual/html_node/Automatic-Variables.html) so you never duplicate filenames:

| Variable | Expands To | Directory / File Variants | Typical Usage | 
| ----- | ----- | ----- | ----- | 
| **`$@`** | Full path of the **target** being built | `$(@D)` (dir), `$(@F)` (filename) | `-o $@`, `ar rcs $@ $^` | 
| **`$<`** | The **first** prerequisite | `$(<D)`, `$(<F)` | Compiling a single `.c` file: `$(CC) -c $< -o $@` | 
| **`$^`** | **All** normal prerequisites (deduplicated) | `$(^D)`, `$(^F)` | Linking all `.o` files into a binary or `.a` archive | 
| **`$+`** | **All** normal prerequisites (**keeping duplicates**) | `$(+D)`, `$(+F)` | Static library link commands where repeated `-la -lb -la` order matters | 
| **`$?`** | Prerequisites **newer** than the target | `$(?D)`, `$(?F)` | Updating only changed members in an archive | 
| **`$*`** | The **stem** matched by `%` in a pattern rule | `$(*D)`, `$(*F)` | Generating sidecar files (`$*.d`, `$*.map`) | 

### B. Pattern Rules vs. Static Pattern Rules

As documented in [Pattern Rules (GNU make)](https://www.gnu.org/software/make/manual/html_node/Pattern-Rules.html):

* **General Pattern Rule**: Applies to *any* file matching the pattern:

  ```
  build/obj/%.o: src/%.c
  	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
  
  ```

* **Static Pattern Rule**: Restricts a pattern rule to an explicit list of targets (`TARGETS: TARGET-PATTERN: PREREQ-PATTERNS`). This is crucial when you compile library `.o` files with `-fPIC -fvisibility=hidden` and executable `.o` files with different flags from the **same** `src/` directory:

  ```
  $(LIB_PIC_OBJS): build/obj/pic/%.o: src/%.c | build/obj/pic
  	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -c $< -o $@
  
  ```

### C. Automatic Header Dependency Generation (`-MMD -MP`)

If `src/log.c` has `#include "syswatch.h"` and you write a rule `build/obj/log.o: src/log.c`, editing `include/syswatch.h` will **not** recompile `log.o` unless `make` knows `log.o` depends on `include/syswatch.h`!
Instead of maintaining header lists by hand, pass `-MMD -MP` to `gcc` (documented in [GCC Preprocessor Options](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html)):

* **`-MMD`**: Tells the preprocessor to emit a Makefile rule (`build/obj/log.o: src/log.c include/syswatch.h`) into `build/obj/log.d` alongside compilation, omitting system headers (`<stdio.h>`).

* **`-MP`**: Emits empty phony rules (`include/syswatch.h:`) for each header so `make` does not error out with *"No rule to make target"* if a header file is later renamed or deleted.

* **`-include $(DEPS)`**: Placed at the bottom of your `Makefile` (with a leading `-` so `make` silently ignores missing `.d` files on a clean build).

## 4. Built-in Rules

GNU `make` ships with \~90 built-in implicit rules for C, C++, assembly, Yacc, Lex, and `ar` archives, detailed in [Implicit Rules (GNU make)](https://www.gnu.org/software/make/manual/html_node/Implicit-Rules.html) and [Catalogue of Rules (GNU make)](https://www.gnu.org/software/make/manual/html_node/Catalogue-of-Rules.html).

### How Built-In C Rules Work

Even with an **empty** `Makefile`, running `make hello` when `hello.c` exists works because `make` uses its built-in recipes:

* **Compiling `.c` → `.o` (`COMPILE.c`)**:
  `$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<`

* **Linking `.o` → executable (`LINK.o`)**:
  `$(CC) $(LDFLAGS) $(TARGET_ARCH) $^ $(LOADLIBES) $(LDLIBS) -o $@`

### The Critical Distinction Between `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, and `LDLIBS`

| Standard Variable | Passed To | Examples | Why Placement Matters | 
| ----- | ----- | ----- | ----- | 
| **`CPPFLAGS`** | C Preprocessor (`cpp`, `cc1`) | `-Iinclude -D_GNU_SOURCE -MMD -MP` | Header search paths and `-D` macros belong here so both C, C++, and `.S` assembly rules receive them. | 
| **`CFLAGS`** | C Compiler (`cc1`) | `-std=c11 -Wall -Wextra -Werror -O2 -ggdb3` | Language dialect, warnings, optimization, and debug flags. | 
| **`LDFLAGS`** | Linker (`ld` via `gcc`) — **before** objects | `-Lbuild/lib -Wl,-rpath,'$$ORIGIN/../lib'` | Library search paths (`-L`) and linker options (`-Wl,...`). Note: escape `$ORIGIN` with `$$ORIGIN` inside a `Makefile` so `make` passes literal `$ORIGIN` to the shell! | 
| **`LDLIBS`** | Linker (`ld` via `gcc`) — **after** objects | `-lsyswatch -ldl -lpthread` | Library names (`-l`). Must appear in `LDLIBS` (not `LDFLAGS`!) so they are placed **after** `.o` files on the command line. | 

### Inspecting or Disabling Built-In Rules

* Run `make -p -f /dev/null | less` in your container to inspect `make`'s entire built-in rule database.

* In production systems `Makefile`s, add `MAKEFLAGS += --no-builtin-rules` and `.SUFFIXES:` at the top of your `Makefile` to disable legacy RCS/SCCS/Modula-2 implicit searches and make `make --debug=b` output crystal clear.

## 5. Chapter 3 Hands-On Labs & `syswatch` Project Milestone 3

### Part A: Quick Lab — Watch Built-In Rules vs. `--no-builtin-rules`

In a scratch directory, create a 1-line `foo.c` (`int main(void){return 0;}`) with **no `Makefile`** and run:

```
make -n foo          # Watch make's built-in rule expand: cc foo.c -o foo
make -d foo | head -n 45  # Watch make check for foo.c, foo.p, foo.f, RCS/foo.c,v, s.foo.c!

```

### Part B: Cumulative Mini-Project Milestone 3 — Production `Makefile` for `syswatch`

Now let's replace manual `gcc`, `ar`, and `ln -sf` commands in `/workdir/syswatch` with a single, clean, parallel-safe (`make -j$(nproc)`) **`Makefile`**.

#### Requirements for Your `/workdir/syswatch/Makefile`

1. **Disable Built-In Clutter & Enable Strict Shell Hygiene**:

   * Start with `MAKEFLAGS += --no-builtin-rules` and `.SUFFIXES:`.

   * Use `.DELETE_ON_ERROR:` so if any recipe fails partway through, `make` deletes the corrupted target file automatically.

2. **Support `BUILD=debug` (default) and `BUILD=release` Conditionals**:

   * Use `BUILD ?= debug` and `ifeq ($(BUILD),debug)` vs `else`:

     * In `debug` mode: add `-O0 -ggdb3 -DENABLE_DEBUG_LOG -DSYSWATCH_VERSION=\"0.2.0-debug\"`.

     * In `release` mode: add `-O2 -g -DNDEBUG -DSYSWATCH_VERSION=\"0.2.0\"`.

   * Always include `-std=c11 -Wall -Wextra -Werror -Wshadow -Wformat=2` in `CFLAGS` and `-Iinclude -D_GNU_SOURCE -MMD -MP` in `CPPFLAGS`.

3. **Targets to Build Under `all` (Default Goal)**:

   * **Static Library**: `build/lib/libsyswatch.a` (built from `build/obj/static/log.o` and `build/obj/static/error.o` using `$(AR) rcs $@ $^`).

   * **Shared Library & Symlinks**: `build/lib/libsyswatch.so.1.0.0` (built from PIC objects `build/obj/pic/log.o` and `build/obj/pic/error.o` compiled with `-fPIC -fvisibility=hidden` and linked with `-shared -Wl,-soname,libsyswatch.so.1`), plus rules for `build/lib/libsyswatch.so.1` and `build/lib/libsyswatch.so`.

   * **Runtime Plugin**: `build/lib/plugin_dummy_cpu.so` compiled from `plugins/plugin_dummy_cpu.c` with `-shared -fPIC`.

   * **Daemon & CLI Executables**:

     * `build/bin/syswatchd` (from `build/obj/bin/syswatchd.o`, linked against `build/lib/libsyswatch.so` and `-ldl` with `-Wl,-rpath,'$$ORIGIN/../lib'`).

     * `build/bin/syswatchctl` (from `build/obj/bin/syswatchctl.o`, linked against `build/lib/libsyswatch.so` with `-Wl,-rpath,'$$ORIGIN/../lib'`).
       *(Tip: Remember to escape `$ORIGIN` as `$$ORIGIN` inside a `Makefile`!)*

4. **Order-Only Directory Creation & Auto-Dependencies**:

   * Use order-only prerequisites (`| build/obj/pic build/lib build/bin ...`) to `mkdir -p $@` all output directories automatically.

   * Include all generated `.d` dependency files at the bottom via `-include $(ALL_DEPS)`.

5. **Phony Targets (`.PHONY: all check clean info`)**:

   * `make all` (builds everything—test with `make -j$(nproc)`).

   * `make check` (runs `./build/bin/syswatchd ./build/lib/plugin_dummy_cpu.so` and `./build/bin/syswatchctl`).

   * `make info` (prints `BUILD`, `CC`, `CFLAGS`, `LDFLAGS`, and discovered object/dependency lists).

   * `make clean` (removes `build/`).

#### How to Verify Your `Makefile` in Your Container

1. Run `make clean && make -j$(nproc) all check`.

2. Run `make` a second time immediately and verify it reports **`Nothing to be done for 'all'`** (proving zero phony-prerequisite rebuild bugs!).

3. Run `touch include/syswatch.h && make` and verify that `-MMD -MP` automatically recompiles the `.o` files that include `syswatch.h`.

4. Run `make clean && make BUILD=release all check` to test your conditional release build.