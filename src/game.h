// game.h
// The state of the whole game and the rules that tie everything together: score, lives,
// levels, fruit, collisions, and what happens on each frame.
//
// All of the game's changing data lives in one struct, `game`. Keeping it in one place
// makes it easy to see everything a frame can change, and to reset it.
//
// C lesson: `game` is a global variable, shared by every file that includes this header.
// For a small program with exactly one game going on, that keeps the code short. Larger
// programs usually pass a pointer to their state into each function instead, which makes
// it explicit what each function reads and changes, and lets you run two at once.
#ifndef PACMAN_GAME_H
#define PACMAN_GAME_H

#include <stdbool.h>

#include "config.h"
#include "ghosts.h"
#include "player.h"

// The game is always in exactly one of these states. Each frame, game_update() does
// what the current state calls for. This pattern is called a "state machine".
typedef enum {
    STATE_READY,      // READY! shows; nothing moves yet
    STATE_PLAYING,
    STATE_DYING,      // caught: a short freeze, then the death animation
    STATE_LEVEL_DONE, // every pellet eaten: the maze flashes
    STATE_GAME_OVER,  // waiting for Enter
} GameState;

typedef struct {
    Pacman pac;
    Ghost ghosts[NUM_GHOSTS];

    GameState state;
    int state_timer; // frames left in the current state
    int frame;       // frames since the program started; drives blinking and animations
    bool paused;

    int score, high_score, lives, level;
    bool extra_life_awarded;

    int dots_left;        // pellets still in the maze; 0 ends the level
    int dots_eaten_level; // pellets eaten this level: brings out the fruit
    int dots_eaten_life;  // pellets eaten since the last death: releases ghosts
    int no_dot_timer;     // frames since the last pellet: also releases ghosts
    int speed_pellets;    // pellets eaten since the last death, across levels: speeds Pac-Man up

    int mode_index, mode_timer; // where we are in the scatter/chase schedule (ghosts.c)
    bool chase;                 // true in chase mode, false in scatter mode

    int fright_timer;       // frames the ghosts stay blue
    int ghosts_eaten_combo; // ghosts eaten on this power pellet: 200, 400, 800, 1600
    int freeze_timer;       // frames the action stops after eating a ghost

    int fruit_timer; // frames the fruit stays out; 0 when there is none

    // The points that show up for a moment where a ghost or fruit was eaten
    int popup_timer, popup_x, popup_y, popup_points;
} Game;

// C lesson: this declares the variable; game.c defines it, which creates it.
extern Game game;

// Starts a new game from level 1.
void game_new(void);

// Advances the game by one frame (1/60 of a second).
void game_update(void);

// Player input
void game_steer(Dir dir);
void game_toggle_pause(void);

// Pac-Man ate a pellet worth `points`.
void game_pellet_eaten(int points);

// 0 on level 1, 1 on levels 2 to 4, 2 from level 5 on. Speeds go up with the tier.
int game_speed_tier(void);

// Turns a speed in pixels per frame into a whole number of pixels to move this frame.
//
// A speed like 3.03 can't be moved in one frame, since positions are whole pixels. So
// each frame adds the speed to an accumulator, moves the whole part, and keeps the
// fraction for later: 3, 3, 3, ..., and a 4 once the fractions add up to a pixel. Over a
// second the total is exactly right.
int take_steps(float *acc, float speed);

#endif
