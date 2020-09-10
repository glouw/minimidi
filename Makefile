all:
	gcc main.c -Wall -Wextra -Wpedantic -std=gnu99 -Ofast -march=native -lm -lSDL2 -o minimidi
