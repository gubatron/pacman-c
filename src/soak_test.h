// soak_test.h
// A headless test that plays the game with a bot. Only built by `make test`, which
// compiles with -DPACMAN_DEBUG.
#ifndef PACMAN_SOAK_TEST_H
#define PACMAN_SOAK_TEST_H

// Plays `frames` frames and checks the game's invariants after every one. Saves the last
// frame as a BMP if screenshot_path isn't NULL. Returns 0 on success, 1 on failure.
int soak_test_run(int frames, const char *screenshot_path);

#endif
