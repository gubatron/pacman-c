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

.PHONY: run test web serve clean help

pacman: $(SRC) $(HEADERS) ## Build the game (release, -O2)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(SRC) $(SDL_LIBS)

run: pacman ## Build and play
	./pacman

pacman-test: $(SRC) src/soak_test.c $(HEADERS)
	$(CC) $(CFLAGS) -DPACMAN_DEBUG $(SDL_CFLAGS) -o $@ $(SRC) src/soak_test.c $(SDL_LIBS)

test: pacman-test ## Let a bot play 200,000 frames without a window, checking invariants
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy PACMAN_FRAMES=200000 ./pacman-test

# The WebAssembly build for web browsers, using Emscripten (brew install emscripten).
# It compiles the same C files with emcc, uses Emscripten's own builds of SDL2 and
# SDL2_ttf, and packs font.ttf into the download. Upload the files in build/web/ to any
# web server folder, for example https://gubatron.com/pacman-c/.
WEB_FLAGS := -O2 -Wall -Wextra -std=gnu11 -sUSE_SDL=2 -sUSE_SDL_TTF=2 -sALLOW_MEMORY_GROWTH=1 \
	-sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=callMain

build/web/index.html: $(SRC) $(HEADERS) web/shell.html font.ttf
	mkdir -p build/web
	emcc $(WEB_FLAGS) --preload-file font.ttf --shell-file web/shell.html -o $@ $(SRC)

web: build/web/index.html ## Build the browser version into build/web/

serve: web ## Build the browser version and serve it at http://localhost:8000
	python3 -m http.server 8000 --directory build/web

clean: ## Remove the binaries and the web build
	rm -rf pacman pacman-test build

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  %-8s %s\n", $$1, $$2}'
