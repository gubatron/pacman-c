// main.c
// Pac-Man in C with SDL2: the classic maze, pellets and power pellets, four ghosts
// with their chase/scatter personalities, fruit, lives, levels, sound and a saved high score.
// Controls: arrows or WASD to move, P to pause, M to mute, Enter to play again, Esc to quit.
//
// This file starts everything up, runs the main loop, and shuts everything down. A good
// order to read the rest of the code in:
//
//   config.h     the shared numbers: maze size, frame rate, landmarks
//   maze.h/.c    the grid, directions, and tile geometry
//   game.h/.c    the game state and the rules, frame by frame
//   player.c     how Pac-Man moves and eats
//   ghosts.c     the ghost AI
//   render.c     drawing everything
//   sound.c      the synthesizer
//   highscore.c  saving the high score
//   soak_test.c  the automated test
//
// Each .h file is a module's public interface: what other files may use. The matching
// .c file has the implementation, and anything marked `static` in it is private to it.
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "game.h"
#include "highscore.h"
#include "render.h"
#include "sound.h"
#ifdef PACMAN_DEBUG
#include "soak_test.h"
#endif

// Reacts to a key press. Sets *quit to ask the main loop to stop.
static void handle_keydown(SDL_Scancode key, bool *quit) {
    // Scancodes name physical key positions, so WASD stays in the same place on
    // keyboards with other layouts
    switch (key) {
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W:
        game_steer(DIR_UP);
        break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
        game_steer(DIR_DOWN);
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
        game_steer(DIR_LEFT);
        break;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
        game_steer(DIR_RIGHT);
        break;
    case SDL_SCANCODE_P:
        game_toggle_pause();
        break;
    case SDL_SCANCODE_M:
        sound_set_muted(!sound_is_muted());
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_SPACE:
        if (game.state == STATE_GAME_OVER) {
            game_new();
        }
        break;
    case SDL_SCANCODE_ESCAPE:
        *quit = true;
        break;
    default:
        break;
    }
}

// The main loop, with a fixed time step.
//
// The display may refresh 60, 120 or 144 times per second, but the game logic must run
// exactly 60 times per second on every computer, or Pac-Man would be faster on some.
// So we keep the two apart: each time around the loop we measure how much real time has
// passed, add it to an "accumulator", and run one logic step (game_update) for every
// 1/60 of a second in it. Whatever is left over waits for the next time around. Then we
// draw once. On a 120 Hz display that means one logic step every other frame; on a slow
// computer it can mean two steps before a draw.
static void run(void) {
    const double step = 1.0 / FPS;
    double accumulator = 0.0;
    // A high-resolution clock: counter ticks, and how many ticks make a second
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 previous = SDL_GetPerformanceCounter();
    bool quit = false;
    SDL_Event e;

    while (!quit) {
        // Handle every event that came in since the last time around
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) { // the window's close button
                quit = true;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) { // ignore auto-repeat
                handle_keydown(e.key.keysym.scancode, &quit);
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        accumulator += (double)(now - previous) / (double)freq;
        previous = now;
        if (accumulator > 0.25) {
            // After a long stall (the window was dragged, the computer slept) don't try
            // to catch up on seconds of game time at once; just carry on
            accumulator = 0.25;
        }
        while (accumulator >= step) {
            game_update();
            accumulator -= step;
        }

        render_frame(); // waits for the display's refresh (VSYNC), which paces the loop
        SDL_Delay(1);   // and in case VSYNC is off, don't spin the CPU at 100%
    }
}

int main(int argc, char *argv[]) {
    // SDL requires main to take these, even though we don't use them
    (void)argc;
    (void)argv;

    if (!maze_template_is_valid()) {
        printf("The maze template has a row of the wrong width.\n");
        return 1;
    }
    if (!render_init()) {
        printf("Failed to initialize SDL.\n");
        return 1;
    }
    sound_init();
    srand((unsigned)time(NULL)); // seed the random numbers that frightened ghosts use

#ifdef PACMAN_DEBUG
    // C lesson: the preprocessor keeps this block only when compiling with
    // -DPACMAN_DEBUG, so the game itself doesn't carry the test code.
    // It runs before highscore_load(), so the bot's scores never reach your high score file.
    const char *frames_env = getenv("PACMAN_FRAMES");
    if (frames_env != NULL) {
        int result = soak_test_run(atoi(frames_env), getenv("PACMAN_SHOT"));
        sound_quit();
        render_quit();
        SDL_Quit();
        return result;
    }
#endif

    game.high_score = highscore_load();
    game_new();
    run();

    highscore_save(game.high_score);
    sound_quit();
    render_quit();
    SDL_Quit();
    return 0;
}
