CC = gcc
CFLAGS = -O2 -Wall -std=c99
BUILD_DIR = build
TARGET = $(BUILD_DIR)/dtmf_compare
SRC = dtmf_compare.c

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lm

clean:
	rm -f $(TARGET)
