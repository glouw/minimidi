CFLAGS  = -Wall -Wextra -Wpedantic -std=gnu99 -lm -lSDL2
CFLAGS += -g -O0 -march=native

CC = gcc

BIN = minimidi

SRC = main.c

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN)
