// highscore.h
// Remembers the high score between runs, in a small text file.
#ifndef PACMAN_HIGHSCORE_H
#define PACMAN_HIGHSCORE_H

// Returns the saved high score, or 0 if there is none yet.
int highscore_load(void);

// Saves the high score. Does nothing if highscore_load() couldn't find a place for it.
void highscore_save(int high_score);

#endif
