# ==============================================================================
# SYSWATCH Makefile — Milestone 2 (Shared/Static Libraries & Dynamic Plugins)
# ==============================================================================

# Target Names
LIB_NAME        := syswatch
LIB_STATIC      := lib$(LIB_NAME).a
LIB_SHARED_REAL := lib$(LIB_NAME).so.1.0.0
LIB_SHARED_SONAME:= lib$(LIB_NAME).so.1
LIB_SHARED_LINK := lib$(LIB_NAME).so

DAEMON_TARGET   := syswatchd
CTL_TARGET      := syswatchctl
PLUGIN_TARGET   := plugin_dummy_cpu.so

# Directory Structure
BUILD_DIR       := ./build
BUILD_LIB_DIR   := $(BUILD_DIR)/lib
BUILD_BIN_DIR   := $(BUILD_DIR)/bin
SRC_DIR         := ./src
PLUGIN_SRC_DIR  := ./plugins
INC_DIR         := include

# Compiler & Tools
CC              := gcc
AR              := ar
LN              := ln -sf

# Flags
INC_FLAGS       := -I$(INC_DIR)
CPPFLAGS        := $(INC_FLAGS) -MMD -MP
CFLAGS          := -std=c11 -Wall -Wextra -Werror -Wshadow -O2 -g \
                   -fPIC -fvisibility=hidden
LDFLAGS_SO      := -shared -Wl,-soname,$(LIB_SHARED_SONAME)
LDFLAGS_BIN     := -L$(BUILD_LIB_DIR) -l$(LIB_NAME) -Wl,-rpath,'$$ORIGIN/../lib'
LDFLAGS_PLUGIN  := -shared -fPIC

# Source Files
LIB_SRCS        := $(SRC_DIR)/log.c $(SRC_DIR)/error.c
DAEMON_SRCS     := $(SRC_DIR)/syswatchd.c
CTL_SRCS        := $(SRC_DIR)/syswatchctl.c
PLUGIN_SRCS     := $(PLUGIN_SRC_DIR)/plugin_dummy_cpu.c

# Object Files
LIB_OBJS        := $(LIB_SRCS:%.c=$(BUILD_DIR)/%.o)
DAEMON_OBJS     := $(DAEMON_SRCS:%.c=$(BUILD_DIR)/%.o)
CTL_OBJS        := $(CTL_SRCS:%.c=$(BUILD_DIR)/%.o)
PLUGIN_OBJS     := $(PLUGIN_SRCS:%.c=$(BUILD_DIR)/%.o)

ALL_OBJS        := $(LIB_OBJS) $(DAEMON_OBJS) $(CTL_OBJS) $(PLUGIN_OBJS)
DEPS            := $(ALL_OBJS:.o=.d)

# ==============================================================================
# Targets
# ==============================================================================

.PHONY: all clean verify_symbols run_daemon run_ctl

all: $(BUILD_LIB_DIR)/$(LIB_STATIC) \
     $(BUILD_LIB_DIR)/$(LIB_SHARED_REAL) \
     $(BUILD_BIN_DIR)/$(DAEMON_TARGET) \
     $(BUILD_BIN_DIR)/$(CTL_TARGET) \
     $(BUILD_LIB_DIR)/$(PLUGIN_TARGET)

# ------------------------------------------------------------------------------
# 1. Static Library (libsyswatch.a)
# ------------------------------------------------------------------------------
$(BUILD_LIB_DIR)/$(LIB_STATIC): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

# ------------------------------------------------------------------------------
# 2. Shared Library (libsyswatch.so.1.0.0 + Symlinks)
# ------------------------------------------------------------------------------
$(BUILD_LIB_DIR)/$(LIB_SHARED_REAL): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS_SO) $^ -o $@
	$(LN) $(LIB_SHARED_REAL) $(BUILD_LIB_DIR)/$(LIB_SHARED_SONAME)
	$(LN) $(LIB_SHARED_SONAME) $(BUILD_LIB_DIR)/$(LIB_SHARED_LINK)

# ------------------------------------------------------------------------------
# 3. Dynamic Metric Plugin (plugin_dummy_cpu.so)
# ------------------------------------------------------------------------------
$(BUILD_LIB_DIR)/$(PLUGIN_TARGET): $(PLUGIN_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS_PLUGIN) $^ -o $@

# ------------------------------------------------------------------------------
# 4. Binaries (syswatchd & syswatchctl)
# ------------------------------------------------------------------------------
$(BUILD_BIN_DIR)/$(DAEMON_TARGET): $(DAEMON_OBJS) $(BUILD_LIB_DIR)/$(LIB_SHARED_REAL)
	@mkdir -p $(dir $@)
	$(CC) $(DAEMON_OBJS) $(LDFLAGS_BIN) -ldl -o $@

$(BUILD_BIN_DIR)/$(CTL_TARGET): $(CTL_OBJS) $(BUILD_LIB_DIR)/$(LIB_SHARED_REAL)
	@mkdir -p $(dir $@)
	$(CC) $(CTL_OBJS) $(LDFLAGS_BIN) -o $@

# ------------------------------------------------------------------------------
# Compilation Rules
# ------------------------------------------------------------------------------
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# ------------------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------------------
verify_symbols: all
	@echo "=== Checking exported symbols in $(LIB_SHARED_REAL) ==="
	nm -D --defined-only $(BUILD_LIB_DIR)/$(LIB_SHARED_REAL)
	@echo "=== Checking DT_RUNPATH and DT_NEEDED for syswatchd ==="
	readelf -d $(BUILD_BIN_DIR)/$(DAEMON_TARGET) | grep -E '(NEEDED|RUNPATH|RPATH)'

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)