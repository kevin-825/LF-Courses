
# Directory Structure & Executable Names
TARGET_TEST    := syswatch_test
TARGET_RELEASE := syswatch_release

BUILD_DIR      := ./build
SRC_DIRS       := ./src


# Find all the C and C++ files we want to compile
# Note the single quotes around the * expressions. 
# The shell will incorrectly expand these otherwise, but we want to send the * directly to the find command.
SRCS := $(shell find $(SRC_DIRS) -name '*.cpp' -or -name '*.c' -or -name '*.s')

# Prepends BUILD_DIR and appends .o to every src file
# As an example, ./your_dir/hello.cpp turns into ./build/./your_dir/hello.cpp.o
#OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
TEST_OBJS      := $(SRCS:%=$(BUILD_DIR)/test/%.o)
RELEASE_OBJS   := $(SRCS:%=$(BUILD_DIR)/release/%.o)

# String substitution (suffix version without %).
# As an example, ./build/hello.cpp.o turns into ./build/hello.cpp.d
#DEPS := $(OBJS:.o=.d)
DEPS           := $(TEST_OBJS:.o=.d) $(RELEASE_OBJS:.o=.d)

# Every folder in ./src will need to be passed to GCC so that it can find header files
INC_DIRS := include $(shell find $(SRC_DIRS) -type d)
# Add a prefix to INC_DIRS. So moduleA would become -ImoduleA. GCC understands this -I flag
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

# The -MMD and -MP flags together generate Makefiles for us!
# These files will have .d instead of .o as the output.
CPPFLAGS ?= $(INC_FLAGS) -MMD -MP

CFLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -Wformat=2 \
          -Wno-error=unused-variable -Wno-error=unused-parameter \
		  -fno-omit-frame-pointer

# Common compiler flags for C++ (if any C++ source is added later)
CXXFLAGS  := -std=c++17 -Wall -Wextra -fno-omit-frame-pointer

LDFLAGS = -Wl,-Map=$@.map

# Milestone 1 Specific Defines
DEBUG_FLAGS    := -O2 -DENABLE_DEBUG_LOG -DSYSWATCH_VERSION=\"0.1.0-alpha\" -ggdb3 -save-temps=obj  
RELEASE_FLAGS  := -O2 -DSYSWATCH_VERSION=\"0.1.0-release\"


.PHONY: all clean test release verify_symbols

all: test release

# Build Test Binary (with ENABLE_DEBUG_LOG)
# Build Test Binary (with ENABLE_DEBUG_LOG)
test: $(BUILD_DIR)/$(TARGET_TEST)

$(BUILD_DIR)/$(TARGET_TEST): $(TEST_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) $(LDFLAGS) $^ -o $@

# Build Release Binary (without ENABLE_DEBUG_LOG)
release: $(BUILD_DIR)/$(TARGET_RELEASE)

$(BUILD_DIR)/$(TARGET_RELEASE): $(RELEASE_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(RELEASE_FLAGS) $(LDFLAGS) $^ -o $@

# Compile Rule for Test Objects
$(BUILD_DIR)/test/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEBUG_FLAGS) -c $< -o $@

$(BUILD_DIR)/test/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEBUG_FLAGS) -c $< -o $@

# Compile Rule for Release Objects
$(BUILD_DIR)/release/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RELEASE_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(RELEASE_FLAGS) -c $< -o $@

# The final build step.
#$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
#	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
#
## Build step for C source
#$(BUILD_DIR)/%.c.o: %.c
#	mkdir -p $(dir $@)
#	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
#
## Build step for C++ source
#$(BUILD_DIR)/%.cpp.o: %.cpp
#	mkdir -p $(dir $@)
#	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
# Helper to verify debug log optimization in release build
verify_symbols: release
	@echo "=== Checking symbol table for syswatch_release ==="
	nm $(BUILD_DIR)/$(TARGET_RELEASE) | grep -i sw_log || echo "Release build compiled cleanly."

.PHONY: clean reball dump_cpp info_macro dump_include_dirs
clean:
	rm -r $(BUILD_DIR)
run_release: release
	@echo "=== Running syswatch_release ==="
	@$(BUILD_DIR)/$(TARGET_RELEASE)

run_test: test
	@echo "=== Running syswatch_test ==="
	@$(BUILD_DIR)/$(TARGET_TEST)

reball:
	@mkdir -p $(BUILD_DIR)
	@echo "=== Rebuilding all object files ==="
	@$(MAKE) clean
	@$(MAKE) all
	@$(MAKE) run_test
	@$(MAKE) run_release

rebt rebd:
	@mkdir -p $(BUILD_DIR)
	@echo "=== Rebuilding test object files ==="
	@$(MAKE) clean
	@$(MAKE) test
	@$(MAKE) run_test
rebr:
	@mkdir -p $(BUILD_DIR)
	@echo "=== Rebuilding release object files ==="
	@$(MAKE) clean
	@$(MAKE) all
	@$(MAKE) release
	@$(MAKE) run_release

dump_cpp:
	$(CC) $(INC_FLAGS) $(CFLAGS) -dM -E - < /dev/null | grep -E '(__linux__|__x86_64__|__riscv|__GNUC__|__STDC_VERSION__)'

info_macro:
	gdb -batch -ex "break main" -ex "run" -ex "info macro SCALE_FACTOR" ${BUILD_DIR}/$(TARGET_EXEC)

dump_include_dirs:
	$(CC) $(INC_FLAGS) $(CFLAGS) -E -v - < /dev/null

# Include the .d makefiles. The - at the front suppresses the errors of missing
# Makefiles. Initially, all the .d files will be missing, and we don't want those
# errors to show up.
-include $(DEPS)
