// soak_test.c
// A "soak test" runs a program for a long time to shake out rare bugs. Here a bot steers
// Pac-Man at random through hundreds of thousands of frames while we check, after every
// frame, things that must always be true ("invariants"): nobody stands inside a wall,
// no ghost leaves the maze, the pellet count never goes negative. Every 5000th frame the
// bot clears the board, to exercise the level transitions too.
//
// It runs without a window or sound when started with SDL_VIDEODRIVER=dummy and
// SDL_AUDIODRIVER=dummy, so it works on servers and in CI. `make test` does all of that.
#include "soak_test.h"

#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "render.h"

// At each tile center, the bot picks a random open direction other than back.
static void steer_bot(void) {
    Pacman *p = &game.pac;
    if (game.state != STATE_PLAYING || !at_tile_center(p->x, p->y)) {
        return;
    }
    int col = tile_col(p->x), row = tile_row(p->y);
    Dir options[4];
    int count = 0;
    for (int d = 0; d < 4; d++) {
        if (d != opposite(p->dir) && !is_wall(col + DX[d], row + DY[d])) {
            options[count++] = (Dir)d;
        }
    }
    p->next_dir = count > 0 ? options[rand() % count] : opposite(p->dir);
}

// Returns false, after saying why, if something that must always hold doesn't.
static bool check_invariants(int frame) {
    const Pacman *p = &game.pac;
    if (at_tile_center(p->x, p->y) && is_wall(tile_col(p->x), tile_row(p->y))) {
        printf("FAIL frame %d: Pac-Man inside a wall at (%d, %d)\n", frame, p->x, p->y);
        return false;
    }
    for (int i = 0; i < NUM_GHOSTS; i++) {
        const Ghost *g = &game.ghosts[i];
        if ((g->state == GHOST_ACTIVE || g->state == GHOST_EYES) && at_tile_center(g->x, g->y) &&
            is_wall(tile_col(g->x), tile_row(g->y))) {
            printf("FAIL frame %d: ghost %d inside a wall at (%d, %d)\n", frame, i, g->x, g->y);
            return false;
        }
        if (g->x < -2 * TILE || g->x > (MAZE_COLS + 2) * TILE || g->y < 0 || g->y > MAZE_ROWS * TILE) {
            printf("FAIL frame %d: ghost %d left the maze at (%d, %d)\n", frame, i, g->x, g->y);
            return false;
        }
    }
    if (game.dots_left < 0) {
        printf("FAIL frame %d: dots_left went negative\n", frame);
        return false;
    }
    return true;
}

int soak_test_run(int frames, const char *screenshot_path) {
    int games = 1, max_level = 1, best_score = 0;
    game_new();
    for (int f = 0; f < frames; f++) {
        steer_bot();
        if (game.state == STATE_PLAYING && f % 5000 == 4999) {
            maze_clear_pellets();
            game.dots_left = 0;
        }

        game_update();

        if (!check_invariants(f)) {
            return 1;
        }
        if (game.level > max_level) max_level = game.level;
        if (game.score > best_score) best_score = game.score;
        if (game.state == STATE_GAME_OVER && f < frames - 1) {
            games++;
            game_new();
        }
    }

    render_frame();
    if (screenshot_path != NULL && !render_save_bmp(screenshot_path)) {
        printf("Couldn't save %s\n", screenshot_path);
    }
    printf("OK %d frames, %d games, max level %d, best score %d, final state %d, dots left %d\n", frames, games,
           max_level, best_score, game.state, game.dots_left);
    return 0;
}
