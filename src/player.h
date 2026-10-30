// player.h
// Pac-Man himself: moving through the maze, turning at corners and eating pellets.
#ifndef PACMAN_PLAYER_H
#define PACMAN_PLAYER_H

#include <stdbool.h>

#include "maze.h"

// How many of Pac-Man's past positions are remembered for his speed trail, one per frame.
#define TRAIL_LENGTH 12

typedef struct {
    int x, y;     // center, in maze pixels
    Dir dir;      // the way he is going (or facing, while stopped against a wall)
    Dir next_dir; // the last way the player pressed, taken at the first tile center where it's open
    float speed_acc; // fractions of a pixel saved up between frames; see take_steps() in game.h
    float burst;     // extra speed from the last pellet, as a fraction of BASE_SPEED; fades to 0
    bool moving;     // false while stopped against a wall
    float mouth_angle;  // how wide the mouth is open, in degrees
    bool mouth_opening; // whether the mouth is opening or closing
    struct {
        int x, y;
    } trail[TRAIL_LENGTH]; // where he was: trail[0] last frame, trail[1] the frame before, ...
} Pacman;

// Puts Pac-Man back at his starting spot, facing left.
void player_reset(void);

// Moves Pac-Man for one frame, eating whatever he passes over.
void player_update(void);

// Pac-Man's speed right now, in pixels per frame. Divide by BASE_SPEED for a percentage.
float player_speed(void);

// The fastest Pac-Man can go, for scaling the speed gauge.
float player_max_speed(void);

// How strongly to draw the speed trail: 0 below the speed where it starts, rising to 1
// as he goes faster.
float player_trail_strength(void);

#endif
