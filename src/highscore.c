// highscore.c
// Reading and writing the high score file.
//
// On the desktop, SDL_GetPrefPath gives each app a folder where it may write its files,
// in the right place for each operating system. On macOS that is
// ~/Library/Application Support/gubatron/pacman-c/ and on Linux
// ~/.local/share/gubatron/pacman-c/.
//
// In a web browser, files a page writes vanish when it reloads, so there the score goes
// in the browser's localStorage instead, a small store that each website keeps.
#include "highscore.h"

#include <SDL2/SDL.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// C lesson (well, C and JavaScript): EM_JS defines a C function whose body is JavaScript.
// Emscripten compiles the C side to WebAssembly and runs the body in the browser.
EM_JS(int, load_from_browser, (void), {
    try {
        return parseInt(localStorage.getItem("pacman-c-highscore") || "0", 10) || 0;
    } catch (e) {
        return 0; // storage can be off, for example in some private windows
    }
});

EM_JS(void, save_in_browser, (int high_score), {
    try {
        localStorage.setItem("pacman-c-highscore", String(high_score));
    } catch (e) {
    }
});

int highscore_load(void) {
    return load_from_browser();
}

void highscore_save(int high_score) {
    save_in_browser(high_score);
}

#else

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

#endif
