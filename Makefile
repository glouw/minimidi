CFLAGS = -O3 -march=native -Wall -Wextra -Wpedantic
LDFLAGS = -lm -lSDL2

CC = gcc -std=c99

BIN = minimidi

SRC = main.c

all:
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(BIN)
