# pacman-c

Pac-Man in C with SDL2.

![pacman-c screenshot](screenshot.png)

## Features

- The classic 28×31 maze with pellets, power pellets and the side tunnels
- Four ghosts with their arcade personalities: Blinky chases you, Pinky ambushes ahead of you, Inky flanks with Blinky and Clyde loses interest up close
- Scatter and chase waves, frightened ghosts after a power pellet, and eyes that run back to the ghost house
- Fruit, an extra life at 10,000 points, levels that speed up, and a saved high score
- Arcade-style sound synthesized in code, with no audio files: intro jingle, waka-waka, siren, ghost and death sounds

## Controls

| Key | Action |
|---|---|
| Arrows or WASD | Move |
| P | Pause |
| M | Mute |
| Enter or Space | Play again after game over |
| Esc | Quit |

## Build and play

You need a C compiler, SDL2 and SDL2_ttf.

macOS (Homebrew):

```sh
brew install sdl2 sdl2_ttf
```

Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libsdl2-ttf-dev
```

Then:

```sh
make run      # build and play
make          # just build ./pacman
make clean    # remove the binary
```

`./run.sh` does the same as `make run`.

The game loads `font.ttf` from the folder it runs in. The high score is saved in SDL's per-user preferences folder (on macOS, `~/Library/Application Support/gubatron/pacman-c/highscore.txt`).

## Headless test

Building with `-DPACMAN_DEBUG` adds a soak test where a bot plays the game without a window and checks the game's invariants every frame:

```sh
clang -O2 -DPACMAN_DEBUG $(sdl2-config --cflags) -o pacman-debug main.c $(sdl2-config --libs) -lSDL2_ttf -lm
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy PACMAN_FRAMES=200000 ./pacman-debug
```

Set `PACMAN_SHOT=shot.bmp` to save the last frame as an image.
