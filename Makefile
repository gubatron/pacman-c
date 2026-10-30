CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs) -lSDL2_ttf -lm

# Every .c file in src/ except the test, which only `make test` builds
SRC := $(filter-out src/soak_test.c,$(wildcard src/*.c))
HEADERS := $(wildcard src/*.h)

# On macOS, build against the SDK that belongs to the selected toolchain. Without this,
# xcrun can hand Xcode's clang a newer Command Line Tools SDK that its linker can't read.
ifeq ($(shell uname),Darwin)
export SDKROOT ?= $(shell xcrun --sdk macosx --show-sdk-path)
endif

.PHONY: run test clean help

pacman: $(SRC) $(HEADERS) ## Build the game (release, -O2)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(SRC) $(SDL_LIBS)

run: pacman ## Build and play
	./pacman

pacman-test: $(SRC) src/soak_test.c $(HEADERS)
	$(CC) $(CFLAGS) -DPACMAN_DEBUG $(SDL_CFLAGS) -o $@ $(SRC) src/soak_test.c $(SDL_LIBS)

test: pacman-test ## Let a bot play 200,000 frames without a window, checking invariants
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy PACMAN_FRAMES=200000 ./pacman-test

clean: ## Remove the binaries
	rm -f pacman pacman-test

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  %-8s %s\n", $$1, $$2}'
