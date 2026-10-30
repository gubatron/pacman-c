// highscore.c
// Reading and writing the high score file.
//
// SDL_GetPrefPath gives each app a folder where it may write its files, in the right
// place for each operating system. On macOS that is
// ~/Library/Application Support/gubatron/pacman-c/ and on Linux
// ~/.local/share/gubatron/pacman-c/.
#include "highscore.h"

#include <SDL2/SDL.h>
#include <stdio.h>

// The full path of the file, or "" if there is nowhere to save it.
static char path[1024];

int highscore_load(void) {
    char *folder = SDL_GetPrefPath("gubatron", "pacman-c");
    if (folder == NULL) {
        return 0;
    }
    // C lesson: snprintf never writes past the size it's given, unlike sprintf, so a
    // very long folder name can't overflow `path`.
    snprintf(path, sizeof(path), "%shighscore.txt", folder);
    SDL_free(folder); // SDL allocated it, so SDL frees it

    int high_score = 0;
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        // fscanf returns how many values it read; anything but 1 means a bad file
        if (fscanf(f, "%d", &high_score) != 1) {
            high_score = 0;
        }
        fclose(f);
    }
    return high_score;
}

void highscore_save(int high_score) {
    if (path[0] == '\0') {
        return;
    }
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", high_score);
        fclose(f);
    }
}
