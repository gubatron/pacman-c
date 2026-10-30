// main.c
// Pac-Man in C with SDL2: the classic maze, pellets and power pellets, four ghosts
// with their chase/scatter personalities, fruit, lives, levels, sound and a saved high score.
// Controls: arrows or WASD to move, P to pause, M to mute, Enter to play again, Esc to quit.
// On a touch screen: swipe to move, tap to play again.
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
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h> // only when building for the web with `make web`
#endif

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

// Swipes on a touch screen steer like the arrow keys. A finger only has to travel
// SWIPE_DISTANCE game pixels (one tile) to count as a swipe, and every further tile of
// travel is measured from where the last swipe ended, so one finger can slide around
// and keep turning without lifting.
#define SWIPE_DISTANCE ((float)TILE)

// The finger we are following. We ignore any others, so resting a second finger on
// the screen doesn't confuse the steering.
static struct {
    bool down;       // is the finger on the screen?
    bool swiped;     // has it steered yet? If not, lifting it is a tap
    SDL_FingerID id; // which finger, since SDL numbers every finger on the screen
    float x, y;      // where we measure the next swipe from, in game pixels
} touch;

// Reacts to a finger touching, moving on, or leaving the screen.
static void handle_touch(const SDL_TouchFingerEvent *f) {
    // Only screens you touch directly. On a Mac, SDL also reports fingers on the
    // trackpad, which would steer whenever a hand rested on it.
    if (SDL_GetTouchDeviceType(f->touchId) != SDL_TOUCH_DEVICE_DIRECT) {
        return;
    }
    // SDL gives finger positions from 0 to 1 across the window; turn them into game pixels
    float x = f->x * WINDOW_WIDTH;
    float y = f->y * WINDOW_HEIGHT;

    if (f->type == SDL_FINGERDOWN) {
        if (!touch.down) {
            touch.down = true;
            touch.swiped = false;
            touch.id = f->fingerId;
            touch.x = x;
            touch.y = y;
        }
        return;
    }
    if (!touch.down || f->fingerId != touch.id) {
        return; // a finger we aren't following
    }
    if (f->type == SDL_FINGERMOTION) {
        float dx = x - touch.x;
        float dy = y - touch.y;
        if (dx * dx + dy * dy < SWIPE_DISTANCE * SWIPE_DISTANCE) {
            return; // not far enough yet
        }
        // Whichever way the finger moved more wins: mostly sideways steers left or right
        if (fabsf(dx) > fabsf(dy)) {
            game_steer(dx > 0 ? DIR_RIGHT : DIR_LEFT);
        } else {
            game_steer(dy > 0 ? DIR_DOWN : DIR_UP);
        }
        touch.swiped = true;
        touch.x = x;
        touch.y = y;
    } else if (f->type == SDL_FINGERUP) {
        touch.down = false;
        if (!touch.swiped && game.state == STATE_GAME_OVER) {
            game_new(); // a tap plays again, like Enter
        }
    }
}

// One pass of the main loop, with a fixed time step. Returns false when it's time to quit.
//
// The display may refresh 60, 120 or 144 times per second, but the game logic must run
// exactly 60 times per second on every computer, or Pac-Man would be faster on some.
// So we keep the two apart: each pass we measure how much real time has passed, add it
// to an "accumulator", and run one logic step (game_update) for every 1/60 of a second
// in it. Whatever is left over waits for the next pass. Then we draw once. On a 120 Hz
// display that means one logic step every other pass; on a slow computer it can mean
// two steps before a draw.
//
// The loop is written as a function that does one pass, rather than as a while loop,
// because a web browser won't let a page loop forever: there, the browser calls this
// function once per screen refresh instead (see main below).
static bool loop_once(void) {
    const double step = 1.0 / FPS;
    // C lesson: `static` local variables keep their values between calls, so the
    // accumulator and the clock survive from one pass to the next. They are set up the
    // first time the function runs.
    static double accumulator = 0.0;
    static Uint64 freq = 0, previous = 0; // a high-resolution clock and its ticks per second
    if (freq == 0) {
        freq = SDL_GetPerformanceFrequency();
        previous = SDL_GetPerformanceCounter();
    }

    // Handle every event that came in since the last pass
    bool quit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e) != 0) {
        if (e.type == SDL_QUIT) { // the window's close button
            quit = true;
        } else if (e.type == SDL_KEYDOWN && !e.key.repeat) { // ignore auto-repeat
            handle_keydown(e.key.keysym.scancode, &quit);
        } else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
            handle_touch(&e.tfinger);
        }
    }

    Uint64 now = SDL_GetPerformanceCounter();
    accumulator += (double)(now - previous) / (double)freq;
    previous = now;
    if (accumulator > 0.25) {
        // After a long stall (the window was dragged, the computer slept, the browser tab
        // was hidden) don't try to catch up on seconds of game time at once; just carry on
        accumulator = 0.25;
    }
    while (accumulator >= step) {
        game_update();
        accumulator -= step;
    }

    render_frame();
    return !quit;
}

#ifdef __EMSCRIPTEN__
// The browser calls this once per screen refresh. There's nothing to quit to on a web
// page, so Esc does nothing there.
static void browser_frame(void) {
    loop_once();
}

// Phones have no P or M key, so the web page shows buttons that call these.
// EMSCRIPTEN_KEEPALIVE keeps a function in the build even though no C code calls it,
// and lets the page's JavaScript call it as Module._web_toggle_pause().
EMSCRIPTEN_KEEPALIVE void web_toggle_pause(void) {
    game_toggle_pause();
}

EMSCRIPTEN_KEEPALIVE void web_toggle_mute(void) {
    sound_set_muted(!sound_is_muted());
}
#endif

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

#ifdef __EMSCRIPTEN__
    // Hand the loop to the browser: 0 means "at the display's refresh rate", and true
    // means this call never returns, so the code below is only for the desktop
    emscripten_set_main_loop(browser_frame, 0, true);
#else
    while (loop_once()) {
        // render_frame() waits for the display's refresh (VSYNC), which paces the loop;
        // in case VSYNC is off, don't spin the CPU at 100%
        SDL_Delay(1);
    }
#endif

    highscore_save(game.high_score);
    sound_quit();
    render_quit();
    SDL_Quit();
    return 0;
}
