CC = gcc
CFLAGS = -O2 -Wall -std=c99
BUILD_DIR = build

# Source files
DTMF_SRC = dtmf.c

# Target executables
DTMF_TARGET = $(BUILD_DIR)/dtmf

# All targets
ALL_TARGETS = $(DTMF_TARGET)

# Default target
all: $(BUILD_DIR) $(ALL_TARGETS)

# Create build directory
$(BUILD_DIR):
	@echo "Creating build directory..."
	@mkdir -p $(BUILD_DIR)

# Refactored version
$(DTMF_TARGET): $(DTMF_SRC) | $(BUILD_DIR)
	@echo "Building DTMF analyzer..."
	$(CC) $(CFLAGS) -o $@ $< -lm

# Individual targets
refactored: $(BUILD_DIR) $(DTMF_TARGET)

enhanced: $(BUILD_DIR) $(ENHANCED_TARGET)

# Test targets
test-refactored: $(DTMF_TARGET)
	@echo "Testing refactored version with sample data/5.pcm:"
	./$(DTMF_TARGET) data/5.pcm

test-enhanced: $(ENHANCED_TARGET)
	@echo "Testing enhanced version with sample data/5.pcm:"
	./$(ENHANCED_TARGET) data/5.pcm

# Demonstration
demo: $(ENHANCED_TARGET)
	@echo "Running windowing demonstration:"
	./demo_windowing.sh

# Clean targets
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(ALL_TARGETS)

clean-all: clean
	@echo "Removing build directory..."
	rm -rf $(BUILD_DIR)

# Help target
help:
	@echo "Available targets:"
	@echo "  all         - Build all versions (default)"
	@echo "  refactored  - Build refactored version only"
	@echo "  enhanced    - Build enhanced version with windowing"
	@echo "  test-*      - Test specific version"
	@echo "  demo        - Run windowing demonstration"
	@echo "  clean       - Remove executables"
	@echo "  clean-all   - Remove build directory"
	@echo "  help        - Show this help message"

# Phony targets
.PHONY: all refactored enhanced test-refactored test-enhanced demo clean clean-all help
