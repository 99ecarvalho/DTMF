CC = gcc
CFLAGS = -O2 -Wall -std=c99
LDFLAGS = -lm
BUILD_DIR = build

# Source files
DTMF_SRC = dtmf.c
DTMF_STREAM_SRC = dtmf_stream.c

# Target executables
DTMF_TARGET = $(BUILD_DIR)/dtmf
DTMF_STREAM_TARGET = $(BUILD_DIR)/dtmf_stream

# All targets
ALL_TARGETS = $(DTMF_TARGET) $(DTMF_STREAM_TARGET)

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

# Streaming version
$(DTMF_STREAM_TARGET): $(DTMF_STREAM_SRC) | $(BUILD_DIR)
	@echo "Building DTMF streaming analyzer..."
	$(CC) $(CFLAGS) -o $@ $< -lm

# Individual targets
refactored: $(BUILD_DIR) $(DTMF_TARGET)

stream: $(BUILD_DIR) $(DTMF_STREAM_TARGET)

enhanced: $(BUILD_DIR) $(ENHANCED_TARGET)

# Test targets
test-refactored: $(DTMF_TARGET)
	@echo "Testing refactored version with sample data/5.pcm:"
	./$(DTMF_TARGET) data/5.pcm

test-stream: $(DTMF_STREAM_TARGET)
	@echo "Testing streaming version with sample data/5.pcm:"
	./$(DTMF_STREAM_TARGET) data/5.pcm

test-stream-stdin: $(DTMF_STREAM_TARGET)
	@echo "Testing streaming version with stdin (first 10 frames):"
	head -c 6400 data/5.pcm | ./$(DTMF_STREAM_TARGET)

test-youtube: $(DTMF_STREAM_TARGET)
	@echo "YouTube streaming test:"
	@echo "Usage: make test-youtube YOUTUBE_URL='https://www.youtube.com/watch?v=VIDEO_ID'"
	@echo "   Or: make test-youtube-cookies YOUTUBE_URL='URL' COOKIES_FILE='cookies.txt'"
	@if [ -z "$(YOUTUBE_URL)" ]; then \
		echo "Error: Please provide YOUTUBE_URL parameter"; \
		echo "Example: make test-youtube YOUTUBE_URL='https://www.youtube.com/watch?v=dQw4w9WgXcQ'"; \
		false; \
	else \
		echo "Streaming YouTube URL: $(YOUTUBE_URL)"; \
		python3 youtube_stream.py "$(YOUTUBE_URL)" | ./$(DTMF_STREAM_TARGET); \
	fi

test-youtube-cookies: $(DTMF_STREAM_TARGET)
	@echo "YouTube streaming test with cookies:"
	@if [ -z "$(YOUTUBE_URL)" ]; then \
		echo "Error: Please provide YOUTUBE_URL parameter"; \
		echo "Example: make test-youtube-cookies YOUTUBE_URL='URL' COOKIES_FILE='cookies.txt'"; \
		false; \
	elif [ -z "$(COOKIES_FILE)" ]; then \
		echo "Error: Please provide COOKIES_FILE parameter"; \
		echo "Example: make test-youtube-cookies YOUTUBE_URL='URL' COOKIES_FILE='cookies.txt'"; \
		false; \
	else \
		echo "Streaming YouTube URL: $(YOUTUBE_URL)"; \
		echo "Using cookies file: $(COOKIES_FILE)"; \
		python3 youtube_stream.py --cookies "$(COOKIES_FILE)" "$(YOUTUBE_URL)" | ./$(DTMF_STREAM_TARGET); \
	fi

test-enhanced: $(ENHANCED_TARGET)
	@echo "Testing enhanced version with sample data/5.pcm:"
	./$(ENHANCED_TARGET) data/5.pcm

# Demonstration
demo: $(ENHANCED_TARGET)
	@echo "Running windowing demonstration:"
	./demo_windowing.sh

demo-stream: $(DTMF_STREAM_TARGET)
	@echo "Running streaming demonstration:"
	./demo_streaming.sh

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
	@echo "  all            - Build all versions (default)"
	@echo "  refactored     - Build refactored version only"
	@echo "  stream         - Build streaming version only"
	@echo "  enhanced       - Build enhanced version with windowing"
	@echo "  test-*         - Test specific version"
	@echo "  test-stream-stdin - Test streaming version with stdin"
	@echo "  test-youtube   - Test YouTube streaming (requires YOUTUBE_URL parameter)"
	@echo "  test-youtube-cookies - Test YouTube streaming with cookies (requires YOUTUBE_URL and COOKIES_FILE)"
	@echo "  demo           - Run windowing demonstration"
	@echo "  demo-stream    - Run streaming demonstration"
	@echo "  clean          - Remove executables"
	@echo "  clean-all      - Remove build directory"
	@echo "  help           - Show this help message"

# Phony targets
.PHONY: all refactored stream enhanced test-refactored test-stream test-stream-stdin test-youtube test-youtube-cookies test-enhanced demo demo-stream clean clean-all help
