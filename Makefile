CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs) -lSDL2_ttf -lm

.PHONY: run clean help

pacman: main.c ## Build the game (release, -O2)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ main.c $(SDL_LIBS)

run: pacman ## Build and play
	./pacman

clean: ## Remove the binary
	rm -f pacman

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  %-8s %s\n", $$1, $$2}'
