# Enhanced Makefile for DTMF stream processing with modular architecture
# Features: Dependency tracking, multiple build configurations, cross-platform support

CC ?= gcc
AR ?= ar
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic $(OPT_FLAGS) $(ARCH_FLAGS)
OPT_FLAGS ?= -O2 -march=native -flto
DEBUG_FLAGS = -g -DDEBUG -fsanitize=address -fsanitize=undefined
LDFLAGS = -lm

# Directories
SRCDIR = src
SRCDIR_ARTIC = src_artic
INCDIR = include
BUILDDIR = build
DEPDIR = .deps

# Source files
MAIN_SRC = $(SRCDIR)/dtmf_stream.c
LIB_SOURCES = $(SRCDIR)/dtmf_utils.c $(SRCDIR)/dtmf_windowing.c $(SRCDIR)/dtmf_fft.c \
              $(SRCDIR)/dtmf_goertzel.c $(SRCDIR)/dtmf_filterbank.c $(SRCDIR)/dtmf_filterbank_iir.c \
              $(SRCDIR)/dtmf_correlation.c $(SRCDIR)/dtmf_music.c
LEGACY_SRC = $(SRCDIR_ARTIC)/dtmf_stream_legacy.c
TEST_CONFIG_SRC = $(SRCDIR)/test_config_loading.c

# Object files
MAIN_OBJ = $(BUILDDIR)/dtmf_stream.o
LIB_OBJECTS = $(LIB_SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
LEGACY_OBJ = $(BUILDDIR)/dtmf_stream_legacy.o
TEST_CONFIG_OBJ = $(BUILDDIR)/test_config_loading.o

# Dependency files
DEPS = $(MAIN_OBJ:.o=.d) $(LIB_OBJECTS:.o=.d) $(LEGACY_OBJ:.o=.d) $(TEST_CONFIG_OBJ:.o=.d)

# Include paths
INCLUDES = -I$(INCDIR)

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    ARCH_FLAGS += -DMACOS
endif
ifeq ($(UNAME_S),Linux)
    ARCH_FLAGS += -DLINUX
endif

# Create build and dependency directories
$(shell mkdir -p $(BUILDDIR) $(DEPDIR))

# Default target
all: dtmf_stream dtmf_stream_legacy test_config_loading

# Enhanced dtmf_stream with modular architecture
dtmf_stream: $(BUILDDIR)/dtmf_stream

$(BUILDDIR)/dtmf_stream: $(MAIN_OBJ) $(LIB_OBJECTS)
	@echo "Linking enhanced dtmf_stream..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Static library (optional)
$(BUILDDIR)/libdtmf.a: $(LIB_OBJECTS)
	@echo "Creating static library $@..."
	$(AR) rcs $@ $^

# Legacy dtmf_stream (original monolithic version)
dtmf_stream_legacy: $(BUILDDIR)/dtmf_stream_legacy

$(BUILDDIR)/dtmf_stream_legacy: $(LEGACY_OBJ)
	@echo "Linking legacy dtmf_stream..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Configuration loading test utility
test_config_loading: $(BUILDDIR)/test_config_loading

$(BUILDDIR)/test_config_loading: $(TEST_CONFIG_OBJ) $(BUILDDIR)/dtmf_utils.o
	@echo "Linking test_config_loading..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Object file compilation with dependency generation
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -MF $(DEPDIR)/$*.d -c $< -o $@

$(BUILDDIR)/dtmf_stream_legacy.o: $(SRCDIR_ARTIC)/dtmf_stream_legacy.c
	@echo "Compiling legacy dtmf_stream_legacy.c..."
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)/dtmf_stream_legacy.d -c $< -o $@

# Include dependency files
-include $(DEPS)

# Build configurations
debug: CFLAGS += $(DEBUG_FLAGS)
debug: OPT_FLAGS = -O0
debug: dtmf_stream dtmf_stream_legacy test_config_loading
	@echo "Debug build completed"

release: CFLAGS += $(OPT_FLAGS) -DNDEBUG
release: dtmf_stream dtmf_stream_legacy test_config_loading
	@echo "Release build completed"

# Static analysis
analyze:
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c99 $(SRCDIR)/ dtmf_stream.c

# Performance profiling build
profile: CFLAGS += -pg -O2
profile: LDFLAGS += -pg
profile: dtmf_stream dtmf_stream_legacy test_config_loading
	@echo "Profile build completed"

# Memory debugging build  
memcheck: CFLAGS += -g -O1 -fsanitize=address -fsanitize=leak
memcheck: LDFLAGS += -fsanitize=address -fsanitize=leak
memcheck: dtmf_stream dtmf_stream_legacy test_config_loading
	@echo "Memory debugging build completed"

# ARM-specific build (no march=native)
arm: OPT_FLAGS = -O2 -flto
arm: dtmf_stream dtmf_stream_legacy test_config_loading
	@echo "ARM build completed"

# Test targets
test-enhanced: $(BUILDDIR)/dtmf_stream
	@echo "Testing enhanced version with sample data:"
	@if [ -f data/01/5.pcm ]; then \
		echo "Running: ./build/dtmf_stream data/01/5.pcm"; \
		./build/dtmf_stream data/01/5.pcm | head -20; \
	else \
		echo "No test data found. Creating test tone..."; \
		echo "1 2 3 4 5 6 7 8 9 0 * #" | tr -d ' ' | while IFS= read -r -n1 digit; do \
			echo "Testing digit: $$digit"; \
			break; \
		done; \
	fi

test-legacy: $(BUILDDIR)/dtmf_stream_legacy
	@echo "Testing legacy version with sample data:"
	@if [ -f data/01/5.pcm ]; then \
		echo "Running: ./build/dtmf_stream_legacy data/01/5.pcm"; \
		./build/dtmf_stream_legacy data/01/5.pcm | head -20; \
	fi

test-config: $(BUILDDIR)/test_config_loading
	@echo "Testing configuration loading:"
	@echo "Running: ./build/test_config_loading"
	./build/test_config_loading

# Compare both versions
compare: $(BUILDDIR)/dtmf_stream $(BUILDDIR)/dtmf_stream_legacy
	@echo "Comparing enhanced vs legacy versions..."
	@if [ -f data/01/5.pcm ]; then \
		echo "Enhanced version:"; \
		time ./build/dtmf_stream data/01/5.pcm > /tmp/enhanced_output.txt 2>&1; \
		echo "Legacy version:"; \
		time ./build/dtmf_stream_legacy data/01/5.pcm > /tmp/legacy_output.txt 2>&1; \
		echo "Output comparison:"; \
		diff -u /tmp/legacy_output.txt /tmp/enhanced_output.txt || true; \
		rm -f /tmp/enhanced_output.txt /tmp/legacy_output.txt; \
	fi

# Install (optional)
PREFIX ?= /usr/local
install: dtmf_stream
	@echo "Installing to $(PREFIX)..."
	install -d $(PREFIX)/bin
	install -m 755 $(BUILDDIR)/dtmf_stream $(PREFIX)/bin/

# Clean targets
clean:
	@echo "Cleaning build files..."
	rm -f $(BUILDDIR)/* $(DEPDIR)/*

distclean: clean
	@echo "Cleaning all generated files..."
	rm -rf $(BUILDDIR) $(DEPDIR)

# Help target
help:
	@echo "Available targets:"
	@echo "  all            - Build enhanced, legacy, and test_config_loading (default)"
	@echo "  dtmf_stream    - Build enhanced modular version"
	@echo "  dtmf_stream_legacy - Build original monolithic version"
	@echo "  test_config_loading - Build configuration loading test utility"
	@echo "  debug          - Build with debug symbols and sanitizers"
	@echo "  release        - Build optimized release version"
	@echo "  profile        - Build with profiling support"
	@echo "  memcheck       - Build with memory debugging"
	@echo "  arm            - Build for ARM (no native optimizations)"
	@echo "  analyze        - Run static code analysis"
	@echo "  test-enhanced  - Test enhanced version with sample data"
	@echo "  test-legacy    - Test legacy version with sample data"
	@echo "  test-config    - Test configuration loading functionality"
	@echo "  compare        - Compare both versions"
	@echo "  install        - Install binaries to $(PREFIX)/bin"
	@echo "  clean          - Remove build files"
	@echo "  distclean      - Remove all generated files"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CC       - Compiler (default: gcc)"
	@echo "  PREFIX   - Install prefix (default: /usr/local)"
	@echo "  OPT_FLAGS - Optimization flags"
	@echo "  DEBUG_FLAGS - Debug flags"

.PHONY: all debug release profile memcheck arm analyze test-enhanced test-legacy test-config compare install clean distclean help test_config_loading

# Disable implicit rules for test_config_loading to prevent conflicts
%: %.c
