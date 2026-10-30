// ghosts.h
// Blinky, Pinky, Inky and Clyde: how each one picks where to go, the scatter/chase
// schedule they share, being frightened by a power pellet, and the ghost house.
#ifndef PACMAN_GHOSTS_H
#define PACMAN_GHOSTS_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "maze.h"

// A ghost's life cycle. Most of the time a ghost is ACTIVE; the other states move it in
// and out of the ghost house along a fixed path instead of through the maze.
//
//   IN_HOUSE --released--> LEAVING --at the door--> ACTIVE --eaten--> EYES
//      ^                      ^                                         |
//      |                      +------ ENTERING <--reached the door------+
//   (start of a life)                 (revived inside the house)
typedef enum {
    GHOST_IN_HOUSE, // bobbing up and down, waiting to be released
    GHOST_LEAVING,  // moving to the door and up out of the house
    GHOST_ACTIVE,   // roaming the maze, chasing Pac-Man (or fleeing, while frightened)
    GHOST_EYES,     // eaten: only the eyes are left, racing back to the house
    GHOST_ENTERING, // the eyes going down through the door to revive
} GhostState;

typedef struct {
    int id;            // 0 Blinky, 1 Pinky, 2 Inky, 3 Clyde; the id picks the personality
    SDL_Color color;
    int x, y;          // center, in maze pixels
    Dir dir;
    GhostState state;
    bool frightened;   // blue, slow, and worth points if Pac-Man catches it
    float speed_acc;   // fractions of a pixel saved up between frames
    int home_x;        // where in the house this ghost waits and revives
    int scatter_col, scatter_row; // the corner tile it heads for in scatter mode
} Ghost;

// Puts the ghosts back in their starting places and restarts the scatter/chase schedule.
void ghosts_reset(void);

// Moves every ghost for one frame.
void ghosts_move(void);

// Runs the scatter/chase schedule and the frightened countdown for one frame.
void ghosts_update_modes(void);

// Lets the next ghost out of the house when it is its turn.
void ghosts_release(void);

// A power pellet was eaten: every ghost turns blue for a while and turns around.
void ghosts_frighten(void);

#endif
