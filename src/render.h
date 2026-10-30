// render.h
// Drawing the game: the window, the maze, the characters, and the text.
//
// Rendering only reads the game state; it never changes it. That separation means the
// game would play exactly the same with no window at all, which is how the headless
// soak test (soak_test.c) runs it.
#ifndef PACMAN_RENDER_H
#define PACMAN_RENDER_H

#include <stdbool.h>

// Starts SDL's video, opens the window and loads the font. Prints why and returns false
// if any of it fails.
bool render_init(void);

// Closes the window and frees what render_init created.
void render_quit(void);

// Draws the current state of the game and shows it on screen.
void render_frame(void);

// Saves what is on screen as a BMP image. Returns false if it couldn't.
bool render_save_bmp(const char *path);

#endif
