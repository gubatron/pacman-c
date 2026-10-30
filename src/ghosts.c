// ghosts.c
// The ghost AI, which is surprisingly simple once you see it.
//
// Every ghost works the same way: whenever it reaches the center of a tile, it looks at
// the open directions (never turning back), and takes the one whose next tile is closest,
// in a straight line, to its *target tile*. That's all. The four personalities only
// differ in how they choose the target tile (see target() below):
//
//   Blinky (red)    targets Pac-Man's tile: he chases.
//   Pinky (pink)    targets 4 tiles in front of Pac-Man: she ambushes.
//   Inky (cyan)     targets a point that depends on Blinky too: he flanks.
//   Clyde (orange)  chases from far away, but retreats to his corner when close.
//
// On top of that, all ghosts share a schedule that alternates between *chase* (use the
// personality target) and *scatter* (each ghost heads for its own corner), so they give
// you short breaks. A power pellet makes them *frightened*: blue, slow, and wandering at
// random. This is the design of the 1980 arcade game.
#include "ghosts.h"

#include <limits.h>
#include <stdlib.h>

#include "game.h"

void ghosts_reset(void) {
    // Starting data for each ghost.
    //
    // The scatter corners are just outside the maze (rows -3 and 31), so a scattering
    // ghost can never reach its target and keeps circling the block near its corner.
    static const struct {
        SDL_Color color;
        int home_x;
        int scatter_col, scatter_row;
        Dir dir;
    } defs[NUM_GHOSTS] = {
        { { 255, 0, 0, 255 }, DOOR_X, 25, -3, DIR_LEFT },              // Blinky, top right
        { { 255, 184, 255, 255 }, DOOR_X, 2, -3, DIR_DOWN },           // Pinky, top left
        { { 0, 255, 255, 255 }, DOOR_X - 2 * TILE, 27, 31, DIR_UP },   // Inky, bottom right
        { { 255, 184, 82, 255 }, DOOR_X + 2 * TILE, 0, 31, DIR_UP },   // Clyde, bottom left
    };

    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        g->id = i;
        g->color = defs[i].color;
        g->home_x = defs[i].home_x;
        g->scatter_col = defs[i].scatter_col;
        g->scatter_row = defs[i].scatter_row;
        g->dir = defs[i].dir;
        g->frightened = false;
        g->speed_acc = 0.0f;
        g->x = defs[i].home_x;
        g->y = HOUSE_Y;
        g->state = GHOST_IN_HOUSE;
    }
    // Blinky starts outside, right above the door
    game.ghosts[0].y = EXIT_Y;
    game.ghosts[0].state = GHOST_ACTIVE;

    // Every life starts with scatter, which is shorter from level 5 on
    game.mode_index = 0;
    game.mode_timer = (game.level >= 5 ? 5 : 7) * FPS;
    game.chase = false;
    game.fright_timer = 0;
}

// ---------------------------------------------------------------------------
// Choosing where to go
// ---------------------------------------------------------------------------

// The tile this ghost is heading for right now. This one function is each ghost's
// whole personality.
//
// C lesson: a function can only return one value, so this "returns" two through
// pointers: the caller passes the addresses of its own tx and ty, and we write there.
static void target(const Ghost *g, int *tx, int *ty) {
    if (g->state == GHOST_EYES) {
        *tx = EYES_TARGET_COL;
        *ty = EYES_TARGET_ROW;
        return;
    }
    if (!game.chase) {
        *tx = g->scatter_col;
        *ty = g->scatter_row;
        return;
    }

    int pc = tile_col(game.pac.x);
    int pr = tile_row(game.pac.y);
    Dir pd = game.pac.dir;
    switch (g->id) {
    case 0: // Blinky chases Pac-Man directly
        *tx = pc;
        *ty = pr;
        break;
    case 1: // Pinky aims four tiles ahead of Pac-Man
        *tx = pc + 4 * DX[pd];
        *ty = pr + 4 * DY[pd];
        break;
    case 2: { // Inky: take the point two tiles ahead of Pac-Man, and double the arrow
              // from Blinky to that point. Inky is dangerous when Blinky is close.
        const Ghost *blinky = &game.ghosts[0];
        int ax = pc + 2 * DX[pd];
        int ay = pr + 2 * DY[pd];
        *tx = 2 * ax - tile_col(blinky->x);
        *ty = 2 * ay - tile_row(blinky->y);
        break;
    }
    default: { // Clyde chases until he gets within 8 tiles, then runs to his corner
        int dx = tile_col(g->x) - pc;
        int dy = tile_row(g->y) - pr;
        if (dx * dx + dy * dy > 64) { // compare squared distances: no square root needed
            *tx = pc;
            *ty = pr;
        } else {
            *tx = g->scatter_col;
            *ty = g->scatter_row;
        }
        break;
    }
    }
}

// Picks the direction to take at the center of tile (col, row): never back the way it
// came, and of the rest, the one whose next tile is closest to the target. Frightened
// ghosts pick at random instead.
static Dir choose_direction(const Ghost *g, int col, int row) {
    Dir options[4];
    int count = 0;
    for (int d = 0; d < 4; d++) {
        if (d == opposite(g->dir) || is_wall(col + DX[d], row + DY[d])) {
            continue;
        }
        options[count++] = (Dir)d;
    }
    if (count == 0) {
        return opposite(g->dir); // a dead end; the maze has none, but be safe
    }
    if (g->frightened && g->state == GHOST_ACTIVE) {
        return options[rand() % count];
    }

    int tx, ty;
    target(g, &tx, &ty);
    Dir best = options[0];
    long best_dist = LONG_MAX;
    for (int i = 0; i < count; i++) {
        long dx = col + DX[options[i]] - tx;
        long dy = row + DY[options[i]] - ty;
        long dist = dx * dx + dy * dy;
        // Strictly less: on a tie the earlier direction wins, which is the arcade's
        // up, left, down, right order from the Dir enum.
        if (dist < best_dist) {
            best_dist = dist;
            best = options[i];
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Moving
// ---------------------------------------------------------------------------

// Moves *value one step closer to target.
static void move_toward(int *value, int target_value) {
    if (*value < target_value) {
        (*value)++;
    } else if (*value > target_value) {
        (*value)--;
    }
}

// Moves a ghost one pixel. Inside the house ghosts follow fixed paths; in the maze they
// decide at each tile center.
static void step(Ghost *g) {
    switch (g->state) {
    case GHOST_IN_HOUSE: // bob up and down until released
        g->y += DY[g->dir];
        if (g->y <= HOUSE_Y - 6) {
            g->dir = DIR_DOWN;
        } else if (g->y >= HOUSE_Y + 6) {
            g->dir = DIR_UP;
        }
        return;
    case GHOST_LEAVING: // to the middle row, across to the door, then up and out
        if (g->x != DOOR_X) {
            if (g->y != HOUSE_Y) {
                move_toward(&g->y, HOUSE_Y);
            } else {
                move_toward(&g->x, DOOR_X);
            }
        } else if (g->y > EXIT_Y) {
            g->y--;
            g->dir = DIR_UP;
        } else {
            g->state = GHOST_ACTIVE;
            g->dir = DIR_LEFT;
        }
        return;
    case GHOST_ENTERING: // the eyes go down through the door to their spot and revive
        if (g->y < HOUSE_Y && g->x != DOOR_X) {
            move_toward(&g->x, DOOR_X);
        } else if (g->y < HOUSE_Y) {
            g->y++;
            g->dir = DIR_DOWN;
        } else if (g->x != g->home_x) {
            move_toward(&g->x, g->home_x);
        } else {
            g->frightened = false;
            g->state = GHOST_LEAVING;
        }
        return;
    case GHOST_ACTIVE:
    case GHOST_EYES:
        break; // these move through the maze, below
    }

    if (at_tile_center(g->x, g->y)) {
        int col = tile_col(g->x);
        int row = tile_row(g->y);
        if (g->state == GHOST_EYES && col == EYES_TARGET_COL && row == EYES_TARGET_ROW) {
            g->state = GHOST_ENTERING; // home: go in through the door
            return;
        }
        g->dir = choose_direction(g, col, row);
    }
    g->x += DX[g->dir];
    g->y += DY[g->dir];
    wrap_x(&g->x);
}

// Pixels per frame for a ghost right now.
static float speed(const Ghost *g) {
    // Fractions of BASE_SPEED for each speed tier (see game_speed_tier())
    static const float NORMAL[] = { 0.75f, 0.85f, 0.95f };
    static const float FRIGHT[] = { 0.50f, 0.55f, 0.60f };
    static const float TUNNEL[] = { 0.40f, 0.45f, 0.50f };
    int tier = game_speed_tier();
    switch (g->state) {
    case GHOST_EYES:
    case GHOST_ENTERING:
        return BASE_SPEED * 1.6f; // eyes hurry home
    case GHOST_IN_HOUSE:
    case GHOST_LEAVING:
        return BASE_SPEED * 0.4f;
    case GHOST_ACTIVE:
        break;
    }
    // The tunnel slows ghosts down, which makes it a good escape route
    int col = tile_col(g->x);
    if (tile_row(g->y) == TUNNEL_ROW && (col <= 5 || col >= MAZE_COLS - 6)) {
        return BASE_SPEED * TUNNEL[tier];
    }
    return BASE_SPEED * (g->frightened ? FRIGHT[tier] : NORMAL[tier]);
}

void ghosts_move(void) {
    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        int steps = take_steps(&g->speed_acc, speed(g));
        for (int s = 0; s < steps; s++) {
            step(g);
        }
    }
}

// ---------------------------------------------------------------------------
// Modes and the ghost house
// ---------------------------------------------------------------------------

void ghosts_update_modes(void) {
    // Seconds of scatter, chase, scatter, chase, ... and the last chase lasts forever
    static const int MODE_SECONDS[] = { 7, 20, 7, 20, 5, 20, 5, -1 };
    const int last = (int)(sizeof(MODE_SECONDS) / sizeof(MODE_SECONDS[0])) - 1;

    if (game.fright_timer > 0) {
        if (--game.fright_timer == 0) {
            for (int i = 0; i < NUM_GHOSTS; i++) {
                game.ghosts[i].frightened = false;
            }
        }
        return; // the scatter/chase clock stops while ghosts are frightened
    }
    if (game.mode_timer > 0 && --game.mode_timer == 0) {
        game.mode_index++;
        game.chase = game.mode_index % 2 == 1; // odd entries are chase
        int seconds = MODE_SECONDS[game.mode_index < last ? game.mode_index : last];
        game.mode_timer = seconds < 0 ? -1 : seconds * FPS; // -1 never counts down to 0
        // Ghosts turn around when the mode changes: the player's cue that it did
        for (int i = 0; i < NUM_GHOSTS; i++) {
            if (game.ghosts[i].state == GHOST_ACTIVE) {
                game.ghosts[i].dir = opposite(game.ghosts[i].dir);
            }
        }
    }
}

// Pinky leaves right away. Inky and Clyde each wait until Pac-Man has eaten a number of
// pellets in this life, or until he goes four seconds without eating any.
void ghosts_release(void) {
    // Pellets to wait for, per ghost, on level 1, level 2, and level 3 on
    static const int RELEASE_DOTS[3][NUM_GHOSTS] = {
        { 0, 0, 30, 60 },
        { 0, 0, 0, 50 },
        { 0, 0, 0, 0 },
    };
    int tier = game.level == 1 ? 0 : (game.level == 2 ? 1 : 2);
    for (int i = 1; i < NUM_GHOSTS; i++) { // Blinky (0) is never in the house at the start
        Ghost *g = &game.ghosts[i];
        if (g->state != GHOST_IN_HOUSE) {
            continue;
        }
        if (game.dots_eaten_life >= RELEASE_DOTS[tier][i] || game.no_dot_timer >= 4 * FPS) {
            g->state = GHOST_LEAVING;
            game.no_dot_timer = 0;
        }
        break; // only the first ghost still inside can be released: they leave in order
    }
}

void ghosts_frighten(void) {
    // Seconds of fright for each level. It shrinks as levels go up, and on level 17 and
    // from level 19 on the ghosts only turn around.
    static const int FRIGHT_SECONDS[] = { 6, 5, 4, 3, 2, 5, 2, 2, 1, 5, 2, 1, 1, 3, 1, 1, 0, 1 };
    const int levels = (int)(sizeof(FRIGHT_SECONDS) / sizeof(FRIGHT_SECONDS[0]));
    int seconds = game.level > levels ? 0 : FRIGHT_SECONDS[game.level - 1];

    game.ghosts_eaten_combo = 0; // the next ghost eaten is worth 200 again
    game.fright_timer = seconds * FPS;
    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        if (g->state == GHOST_ACTIVE) {
            g->dir = opposite(g->dir);
        }
        if (seconds > 0 && g->state != GHOST_EYES && g->state != GHOST_ENTERING) {
            g->frightened = true;
        }
    }
}
