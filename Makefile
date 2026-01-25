CC = gcc
CFLAGS = -O3 -Wall -Wextra -I include -DNDEBUG
SRCS = src/tables.c src/board.c src/movegen.c src/eval.c src/search.c src/uci.c src/main.c
TARGET = engine

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: clean
