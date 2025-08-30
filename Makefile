CC = gcc
CFLAGS = -O2 -Wall -std=c99
BUILD_DIR = build

# Source files
REFACTORED_SRC = dtmf_compare_refactored.c

# Target executables
REFACTORED_TARGET = $(BUILD_DIR)/dtmf_compare_refactored
ENHANCED_TARGET = $(BUILD_DIR)/dtmf_compare_enhanced

# All targets
ALL_TARGETS = $(REFACTORED_TARGET) $(ENHANCED_TARGET)

# Default target
all: $(BUILD_DIR) $(ALL_TARGETS)

# Create build directory
$(BUILD_DIR):
	@echo "Creating build directory..."
	@mkdir -p $(BUILD_DIR)

# Refactored version
$(REFACTORED_TARGET): $(REFACTORED_SRC) | $(BUILD_DIR)
	@echo "Building refactored DTMF analyzer..."
	$(CC) $(CFLAGS) -o $@ $< -lm

# Enhanced version (same source as refactored, different name for clarity)
$(ENHANCED_TARGET): $(REFACTORED_SRC) | $(BUILD_DIR)
	@echo "Building enhanced DTMF analyzer with windowing..."
	$(CC) $(CFLAGS) -o $@ $< -lm

# Individual targets
refactored: $(BUILD_DIR) $(REFACTORED_TARGET)

enhanced: $(BUILD_DIR) $(ENHANCED_TARGET)

# Test targets
test-refactored: $(REFACTORED_TARGET)
	@echo "Testing refactored version with sample data/5.pcm:"
	./$(REFACTORED_TARGET) data/5.pcm

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
