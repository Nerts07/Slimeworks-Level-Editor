CC ?= cc
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null || echo -I/opt/homebrew/opt/raylib/include)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null || echo -L/opt/homebrew/opt/raylib/lib -lraylib)
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11 -Isrc $(RAYLIB_CFLAGS)

.PHONY: all run clean

all: main

SOURCES := $(shell find src -name '*.c')

main: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o main $(RAYLIB_LIBS) -lm

run: main
	./main

clean:
	rm -f main

