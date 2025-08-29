CC = gcc
CFLAGS = -O2 -Wall -std=c99
TARGET = dtmf_compare
SRC = dtmf_compare.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lm

clean:
	rm -f $(TARGET)
