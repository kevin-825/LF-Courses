# Disable built-in rules, clear suffix list, and delete failed targets
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:
.DELETE_ON_ERROR:

# ==============================================================================
# 1. Target Names & Directory Structure
# ==============================================================================
LIB_NAME         := syswatch
LIB_STATIC       := lib$(LIB_NAME).a
LIB_SHARED_REAL  := lib$(LIB_NAME).so.1.0.0
LIB_SHARED_SONAME:= lib$(LIB_NAME).so.1
LIB_SHARED_LINK  := lib$(LIB_NAME).so

DAEMON_TARGET    := syswatchd
CTL_TARGET       := syswatchctl
PLUGIN_TARGET    := plugin_dummy_cpu.so

SRC_DIR          := src
PLUGIN_SRC_DIR   := plugins
INC_DIR          := include

BUILD_DIR        := build
BUILD_LIB_DIR    := $(BUILD_DIR)/lib
BUILD_BIN_DIR    := $(BUILD_DIR)/bin
OBJ_STATIC_DIR   := $(BUILD_DIR)/obj/static
OBJ_PIC_DIR      := $(BUILD_DIR)/obj/pic
OBJ_BIN_DIR      := $(BUILD_DIR)/obj/bin
OBJ_PLUGIN_DIR   := $(BUILD_DIR)/obj/plugin

# Directory list for order-only prerequisites
DIRS             := $(BUILD_LIB_DIR) $(BUILD_BIN_DIR) \
                    $(OBJ_STATIC_DIR) $(OBJ_PIC_DIR) \
                    $(OBJ_BIN_DIR) $(OBJ_PLUGIN_DIR)

# ==============================================================================
# 2. Compiler, Tools & Flags
# ==============================================================================
CC  ?= gcc
AR  := ar
LN  := ln -sf

# Base flags applied to all compilations
CPPFLAGS := -I$(INC_DIR) -D_GNU_SOURCE -MMD -MP
CFLAGS   := -std=c11 -Wall -Wextra -Werror -Wshadow -Wformat=2

# Conditional Build Modes (debug vs release)
BUILD ?= debug
ifeq ($(BUILD),debug)
    CFLAGS += -O0 -ggdb3 -DENABLE_DEBUG_LOG -DSYSWATCH_VERSION=\"0.2.0-debug\"
else ifeq ($(BUILD),release)
    CFLAGS += -O2 -g -DNDEBUG -DSYSWATCH_VERSION=\"0.2.0\"
else
    $(error Invalid BUILD mode '$(BUILD)'. Use 'debug' or 'release')
endif

# Linker Flags
LDFLAGS_SO     := -shared -Wl,-soname,$(LIB_SHARED_SONAME)
LDFLAGS_BIN    := -L$(BUILD_LIB_DIR) -l$(LIB_NAME) -Wl,-rpath,'$$ORIGIN/../lib'
LDFLAGS_PLUGIN := -shared -fPIC

# ==============================================================================
# 3. Source & Object Mapping
# ==============================================================================
LIB_SRCS    := $(SRC_DIR)/log.c $(SRC_DIR)/error.c $(SRC_DIR)/crash.c
DAEMON_SRCS := $(SRC_DIR)/syswatchd.c
CTL_SRCS    := $(SRC_DIR)/syswatchctl.c
PLUGIN_SRCS := $(PLUGIN_SRC_DIR)/plugin_dummy_cpu.c

LIB_STATIC_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_STATIC_DIR)/%.o,$(LIB_SRCS))
LIB_PIC_OBJS    := $(patsubst $(SRC_DIR)/%.c,$(OBJ_PIC_DIR)/%.o,$(LIB_SRCS))
DAEMON_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_BIN_DIR)/%.o,$(DAEMON_SRCS))
CTL_OBJS        := $(patsubst $(SRC_DIR)/%.c,$(OBJ_BIN_DIR)/%.o,$(CTL_SRCS))
PLUGIN_OBJS     := $(patsubst $(PLUGIN_SRC_DIR)/%.c,$(OBJ_PLUGIN_DIR)/%.o,$(PLUGIN_SRCS))

ALL_OBJS        := $(LIB_STATIC_OBJS) $(LIB_PIC_OBJS) $(DAEMON_OBJS) $(CTL_OBJS) $(PLUGIN_OBJS)
ALL_DEPS        := $(ALL_OBJS:.o=.d)

# ==============================================================================
# 4. Targets & Rules
# ==============================================================================
.PHONY: all check info clean dump-check

all: $(BUILD_LIB_DIR)/$(LIB_STATIC) \
     $(BUILD_LIB_DIR)/$(LIB_SHARED_REAL) \
     $(BUILD_LIB_DIR)/$(LIB_SHARED_SONAME) \
     $(BUILD_LIB_DIR)/$(LIB_SHARED_LINK) \
     $(BUILD_LIB_DIR)/$(PLUGIN_TARGET) \
     $(BUILD_BIN_DIR)/$(DAEMON_TARGET) \
     $(BUILD_BIN_DIR)/$(CTL_TARGET)

# Directory Creation Rule
$(DIRS):
	mkdir -p $@

# --- Static Library ---
$(BUILD_LIB_DIR)/$(LIB_STATIC): $(LIB_STATIC_OBJS) | $(BUILD_LIB_DIR)
	$(AR) rcs $@ $^

# --- Shared Library & Symlinks ---
$(BUILD_LIB_DIR)/$(LIB_SHARED_REAL): $(LIB_PIC_OBJS) | $(BUILD_LIB_DIR)
	$(CC) $(LDFLAGS_SO) $^ -o $@

$(BUILD_LIB_DIR)/$(LIB_SHARED_SONAME): $(BUILD_LIB_DIR)/$(LIB_SHARED_REAL)
	$(LN) $(LIB_SHARED_REAL) $@

$(BUILD_LIB_DIR)/$(LIB_SHARED_LINK): $(BUILD_LIB_DIR)/$(LIB_SHARED_SONAME)
	$(LN) $(LIB_SHARED_SONAME) $@

# --- Runtime Plugin ---
$(BUILD_LIB_DIR)/$(PLUGIN_TARGET): $(PLUGIN_OBJS) | $(BUILD_LIB_DIR)
	$(CC) $(LDFLAGS_PLUGIN) $^ -o $@

# --- Executables ---
$(BUILD_BIN_DIR)/$(DAEMON_TARGET): $(DAEMON_OBJS) $(BUILD_LIB_DIR)/$(LIB_SHARED_LINK) | $(BUILD_BIN_DIR)
	$(CC) $(DAEMON_OBJS) $(LDFLAGS_BIN) -ldl -o $@

$(BUILD_BIN_DIR)/$(CTL_TARGET): $(CTL_OBJS) $(BUILD_LIB_DIR)/$(LIB_SHARED_LINK) | $(BUILD_BIN_DIR)
	$(CC) $(CTL_OBJS) $(LDFLAGS_BIN) -o $@

# ==============================================================================
# 5. Static Pattern Compilation Rules
# ==============================================================================
# Static library objects (no PIC)
$(LIB_STATIC_OBJS): $(OBJ_STATIC_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_STATIC_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Shared library objects (with PIC & hidden visibility)
$(LIB_PIC_OBJS): $(OBJ_PIC_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_PIC_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -c $< -o $@

# Binary objects
$(DAEMON_OBJS) $(CTL_OBJS): $(OBJ_BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Plugin objects
$(PLUGIN_OBJS): $(OBJ_PLUGIN_DIR)/%.o: $(PLUGIN_SRC_DIR)/%.c | $(OBJ_PLUGIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

# ==============================================================================
# 6. Helper Targets
# ==============================================================================
check: all
	@echo "=== Running syswatch verification tests ==="
	./$(BUILD_BIN_DIR)/$(DAEMON_TARGET) ./$(BUILD_LIB_DIR)/$(PLUGIN_TARGET)
	./$(BUILD_BIN_DIR)/$(CTL_TARGET)

info:
	@echo "BUILD           : $(BUILD)"
	@echo "CC              : $(CC)"
	@echo "CFLAGS          : $(CFLAGS)"
	@echo "CPPFLAGS        : $(CPPFLAGS)"
	@echo "LDFLAGS_BIN     : $(LDFLAGS_BIN)"
	@echo "LIB_STATIC_OBJS : $(LIB_STATIC_OBJS)"
	@echo "LIB_PIC_OBJS    : $(LIB_PIC_OBJS)"
	@echo "ALL_DEPS        : $(ALL_DEPS)"

clean:
	rm -rf $(BUILD_DIR)

dump-check: build/bin/syswatchd
	@mkdir -p build
	@echo "Checking kernel core_pattern..."
	@cat /proc/sys/kernel/core_pattern
	@echo "Generating core dump via GDB..."
	gdb -batch \
		-ex "b trigger_test_crash" \
		-ex "run --crash-test" \
		-ex "generate-core-file build/syswatchd.core" \
		./build/bin/syswatchd
	@echo "--- Inspecting Core Dump Metadata ---"
	readelf -n build/syswatchd.core
	@echo "--- Running GDB Forensics Checklist ---"
	gdb -batch \
		-ex "bt full" \
		-ex "p \$$siginfo._sifields._sigfault.si_addr" \
		-ex "info registers" \
		./build/bin/syswatchd build/syswatchd.core

-include $(ALL_DEPS)