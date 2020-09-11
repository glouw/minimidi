CFLAGS = -Wall -Wextra -Wpedantic -std=gnu99 -Ofast -march=native -lm -lSDL2

CC = gcc

BIN = minimidi

SRC = main.c

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN)
