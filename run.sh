#!/bin/bash

# run.sh

# Exit immediately if a command exits with a non-zero status.
set -e

# Compile the C code
clang -o pacman main.c \
    `sdl2-config --cflags --libs` \
    -lSDL2_ttf

# Make the script executable
chmod +x pacman

echo "Compilation successful. You can run the game using ./pacman"
