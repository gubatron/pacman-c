CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs) -lSDL2_ttf -lm

# On macOS, build against the SDK that belongs to the selected toolchain. Without this,
# xcrun can hand Xcode's clang a newer Command Line Tools SDK that its linker can't read.
ifeq ($(shell uname),Darwin)
export SDKROOT ?= $(shell xcrun --sdk macosx --show-sdk-path)
endif

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
