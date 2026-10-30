// game.c
// The rules: scoring, lives, levels, fruit, collisions, and the frame-by-frame state
// machine that runs everything else.
#include "game.h"

#include <math.h>
#include <stdlib.h>

#include "highscore.h"
#include "sound.h"

Game game;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

int game_speed_tier(void) {
    return game.level == 1 ? 0 : (game.level <= 4 ? 1 : 2);
}

int take_steps(float *acc, float speed) {
    *acc += speed;
    int steps = (int)*acc; // converting to int drops the fraction
    *acc -= (float)steps;
    return steps;
}

static void add_score(int points) {
    game.score += points;
    if (!game.extra_life_awarded && game.score >= EXTRA_LIFE_SCORE) {
        game.extra_life_awarded = true;
        game.lives++;
        sound_play(SOUND_EXTRA_LIFE);
    }
    if (game.score > game.high_score) {
        game.high_score = game.score;
    }
}

// Shows `points` at (x, y) for a moment.
static void show_popup(int points, int x, int y, int frames) {
    game.popup_points = points;
    game.popup_x = x;
    game.popup_y = y;
    game.popup_timer = frames;
}

// ---------------------------------------------------------------------------
// Starting games, levels and lives
// ---------------------------------------------------------------------------

static void reset_maze(void) {
    game.dots_left = maze_reset();
    game.dots_eaten_level = 0;
}

// Puts everyone back in their starting places, at the start of a level or a new life.
// The maze keeps its pellets.
static void reset_positions(void) {
    player_reset();
    ghosts_reset();
    game.freeze_timer = 0;
    game.fruit_timer = 0;
    game.popup_timer = 0;
    game.dots_eaten_life = 0;
    game.no_dot_timer = 0;
}

static void enter_ready(int frames) {
    game.state = STATE_READY;
    game.state_timer = frames;
}

void game_new(void) {
    game.score = 0;
    game.lives = START_LIVES;
    game.level = 1;
    game.extra_life_awarded = false;
    game.paused = false;
    sound_set_paused(false);
    reset_maze();
    reset_positions();
    enter_ready(4 * FPS);
    sound_play(SOUND_INTRO); // only a new game gets the jingle, like the arcade
}

static void next_level(void) {
    game.level++;
    reset_maze();
    reset_positions();
    enter_ready(2 * FPS);
}

static void after_death(void) {
    game.lives--;
    if (game.lives <= 0) {
        game.state = STATE_GAME_OVER;
        highscore_save(game.high_score);
        return;
    }
    reset_positions();
    enter_ready(2 * FPS);
}

// ---------------------------------------------------------------------------
// Eating
// ---------------------------------------------------------------------------

void game_pellet_eaten(int points) {
    add_score(points);
    game.dots_left--;
    game.dots_eaten_level++;
    game.dots_eaten_life++;
    game.no_dot_timer = 0;
    if (game.dots_eaten_level == 70 || game.dots_eaten_level == 170) {
        game.fruit_timer = FPS * 19 / 2; // fruit appears twice per level, for 9.5 seconds
    }
    sound_play(SOUND_WAKA);
}

// Checks whether Pac-Man touches a ghost or the fruit. Touching a frightened ghost eats
// it; touching any other ghost loses a life.
static void check_collisions(void) {
    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        // Only ghosts out in the maze count. "Touching" means their centers are closer
        // than 3/5 of a tile, measured as |dx| + |dy| (the "Manhattan distance").
        if (g->state != GHOST_ACTIVE || abs(g->x - game.pac.x) + abs(g->y - game.pac.y) >= TILE * 3 / 5) {
            continue;
        }
        if (g->frightened) {
            // Each ghost on the same power pellet is worth double: 200, 400, 800, 1600.
            // Shifting left by n bits multiplies by 2 to the n.
            game.ghosts_eaten_combo++;
            int points = 200 << (game.ghosts_eaten_combo - 1);
            add_score(points);
            g->frightened = false;
            g->state = GHOST_EYES;
            show_popup(points, g->x, g->y, FPS);
            game.freeze_timer = FPS / 2;
            sound_play(SOUND_GHOST_EATEN);
        } else {
            game.state = STATE_DYING;
            game.state_timer = 2 * FPS;
            return;
        }
    }

    if (game.fruit_timer > 0 && abs(game.pac.x - FRUIT_X) < TILE / 2 && abs(game.pac.y - FRUIT_Y) < TILE / 2) {
        // Points for the fruit on each level; from level 13 on it is always 5000
        static const int FRUIT_POINTS[] = { 100, 300, 500, 500, 700, 700, 1000, 1000, 2000, 2000, 3000, 3000, 5000 };
        const int count = (int)(sizeof(FRUIT_POINTS) / sizeof(FRUIT_POINTS[0]));
        int points = FRUIT_POINTS[game.level <= count ? game.level - 1 : count - 1];
        add_score(points);
        game.fruit_timer = 0;
        show_popup(points, FRUIT_X, FRUIT_Y, FPS * 3 / 2);
        sound_play(SOUND_FRUIT);
    }
}

// ---------------------------------------------------------------------------
// One frame
// ---------------------------------------------------------------------------

// Picks the background sound for what is happening now. The siren plays while Pac-Man
// is out there, the frightened sound while a ghost is blue, and the eyes sound while
// eaten ghosts head home, which beats both.
static void update_loop_sound(void) {
    Sound loop = SOUND_NONE;
    if (game.state == STATE_PLAYING) {
        loop = SOUND_SIREN;
        for (int i = 0; i < NUM_GHOSTS; i++) {
            const Ghost *g = &game.ghosts[i];
            if (g->state == GHOST_EYES || g->state == GHOST_ENTERING) {
                loop = SOUND_EYES;
                break;
            }
            if (g->frightened && game.fright_timer > 0) {
                loop = SOUND_FRIGHT;
            }
        }
    }
    // The siren steps up four times as the maze empties: 0, 0.25, 0.5 and 0.75
    int total = game.dots_left + game.dots_eaten_level;
    float pitch = total > 0 ? floorf(4.0f * (float)game.dots_eaten_level / (float)total) / 4.0f : 0.0f;
    sound_set_loop(loop, pitch);
}

// The rules for one frame. The order matters: for example Pac-Man moves before the
// collision check, and ghosts are checked again after they move, so nobody can pass
// through anyone else between two frames.
static void update_rules(void) {
    game.frame++;
    if (game.popup_timer > 0) {
        game.popup_timer--;
    }

    // States other than PLAYING just count down and then move on
    switch (game.state) {
    case STATE_READY:
        if (--game.state_timer <= 0) {
            game.state = STATE_PLAYING;
        }
        return;
    case STATE_DYING:
        if (--game.state_timer <= 0) {
            after_death();
        } else if (game.state_timer == 3 * FPS / 2) {
            sound_play(SOUND_DEATH); // as the death animation starts, after a short freeze
        }
        return;
    case STATE_LEVEL_DONE:
        if (--game.state_timer <= 0) {
            next_level();
        }
        return;
    case STATE_GAME_OVER:
        return;
    case STATE_PLAYING:
        break;
    }

    if (game.paused) {
        return;
    }
    if (game.freeze_timer > 0) { // a ghost was just eaten: hold still for a moment
        game.freeze_timer--;
        return;
    }

    ghosts_update_modes();

    player_update();
    if (game.dots_left == 0) {
        game.state = STATE_LEVEL_DONE;
        game.state_timer = 5 * FPS / 2;
        return;
    }
    check_collisions();
    if (game.state != STATE_PLAYING) {
        return; // caught
    }

    ghosts_move();
    game.no_dot_timer++;
    ghosts_release();
    check_collisions();

    if (game.fruit_timer > 0) {
        game.fruit_timer--;
    }
}

void game_update(void) {
    update_rules();
    update_loop_sound();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void game_steer(Dir dir) {
    game.pac.next_dir = dir; // taken at the next tile center where it is open (player.c)
}

void game_toggle_pause(void) {
    if (game.state == STATE_PLAYING) {
        game.paused = !game.paused;
        sound_set_paused(game.paused);
    }
}
