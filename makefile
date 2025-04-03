CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = main

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET) fifo1 fifo2 logs.txt

test:
	./$(TARGET) 10 20