// player.c
// How Pac-Man moves.
//
// Movement in this game is done one whole pixel at a time. A speed like 3.03 pixels per
// frame becomes 3 steps on most frames and 4 now and then (see take_steps() in game.c).
// Moving in single pixels means Pac-Man always passes exactly through every tile center,
// which is where all the decisions happen: turning, stopping at walls and eating.
#include "player.h"

#include "game.h"
#include "ghosts.h"

#define MOUTH_SPEED 8.0f // degrees per frame the mouth opens or closes

// Each pellet eaten in a level adds this fraction of BASE_SPEED, so by the last of the
// 244 pellets Pac-Man runs about 24% faster. The bonus lasts through lost lives and
// starts over with the next level. This is our own twist: the arcade has no such rule.
#define SPEED_PER_PELLET 0.001f
#define LEVEL_PELLETS    244

// Speeds as fractions of BASE_SPEED, for each speed tier (see game_speed_tier()).
// Pac-Man is faster while the ghosts are frightened.
static const float NORMAL_SPEED[] = { 0.80f, 0.90f, 1.00f };
static const float FRIGHT_SPEED[] = { 0.90f, 0.95f, 1.00f };

void player_reset(void) {
    Pacman *p = &game.pac;
    p->x = 14 * TILE; // between columns 13 and 14, like the arcade
    p->y = 23 * TILE + TILE / 2;
    p->dir = DIR_LEFT;
    p->next_dir = DIR_NONE;
    p->speed_acc = 0.0f;
    p->moving = false;
    p->mouth_angle = 30.0f;
    p->mouth_opening = false;
}

// Eats the pellet on the tile Pac-Man is on, if there is one.
static void eat(void) {
    Cell eaten = maze_eat(tile_col(game.pac.x), tile_row(game.pac.y));
    if (eaten == CELL_PELLET) {
        game_pellet_eaten(10);
    } else if (eaten == CELL_POWER_PELLET) {
        game_pellet_eaten(50);
        ghosts_frighten();
    }
}

// Moves Pac-Man one pixel. Returns false if a wall stopped him.
//
// Reversing is allowed anywhere, so the controls feel instant. Other turns wait for the
// next tile center. Until then the player's choice waits in next_dir, so pressing a
// direction slightly before a corner still takes the corner: that's "input buffering",
// and it is why Pac-Man feels smooth to steer.
static bool step(void) {
    Pacman *p = &game.pac;
    if (p->next_dir != DIR_NONE && p->next_dir == opposite(p->dir)) {
        p->dir = p->next_dir;
    }
    if (at_tile_center(p->x, p->y)) {
        int col = tile_col(p->x);
        int row = tile_row(p->y);
        if (p->next_dir != DIR_NONE && !is_wall(col + DX[p->next_dir], row + DY[p->next_dir])) {
            p->dir = p->next_dir;
        }
        if (is_wall(col + DX[p->dir], row + DY[p->dir])) {
            p->moving = false;
            return false;
        }
    }
    p->moving = true;
    p->x += DX[p->dir];
    p->y += DY[p->dir];
    wrap_x(&p->x);
    return true;
}

// The mouth swings between nearly closed (5 degrees) and wide open (70 degrees).
static void animate_mouth(void) {
    Pacman *p = &game.pac;
    if (p->mouth_opening) {
        p->mouth_angle += MOUTH_SPEED;
        if (p->mouth_angle >= 70.0f) {
            p->mouth_angle = 70.0f;
            p->mouth_opening = false;
        }
    } else {
        p->mouth_angle -= MOUTH_SPEED;
        if (p->mouth_angle <= 5.0f) {
            p->mouth_angle = 5.0f;
            p->mouth_opening = true;
        }
    }
}

float player_speed(void) {
    int tier = game_speed_tier();
    float pct = game.fright_timer > 0 ? FRIGHT_SPEED[tier] : NORMAL_SPEED[tier];
    return BASE_SPEED * (pct + SPEED_PER_PELLET * (float)game.dots_eaten_level);
}

float player_max_speed(void) {
    // Top tier, with every pellet of a level eaten
    return BASE_SPEED * (1.00f + SPEED_PER_PELLET * LEVEL_PELLETS);
}

void player_update(void) {
    int steps = take_steps(&game.pac.speed_acc, player_speed());
    for (int i = 0; i < steps; i++) {
        if (!step()) {
            break;
        }
        eat(); // after every pixel, so no pellet is skipped
    }
    if (game.pac.moving) {
        animate_mouth(); // the mouth stops when Pac-Man does
    }
}
