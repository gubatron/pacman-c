// maze.h
// The maze: the grid of walls and pellets, the four directions, and the geometry every
// moving thing needs (which tile am I on, am I at its center, is there a wall there).
//
// The grid itself is private to maze.c. Other files read and change it only through the
// functions below, so the rules for what a tile can hold live in one place. That is the
// main way C programs hide data: keep it static inside one .c file and expose functions.
#ifndef PACMAN_MAZE_H
#define PACMAN_MAZE_H

#include <stdbool.h>

#include "config.h"

// What a tile can hold. These are the same characters used to draw the maze in maze.c,
// so the template there reads like a picture of the level.
typedef enum {
    CELL_WALL = '#',
    CELL_PELLET = '.',
    CELL_POWER_PELLET = 'o',
    CELL_DOOR = '-', // the ghost house door: a wall for Pac-Man and for ghosts finding their way
    CELL_EMPTY = ' ',
} Cell;

// The four directions, plus DIR_NONE for "no direction yet".
//
// They are listed in the order the arcade breaks ties when a ghost has two equally good
// turns: up, then left, then down, then right. Because of that order, the opposite of a
// direction is always two steps away: (d + 2) % 4.
typedef enum { DIR_NONE = -1, DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT } Dir;

// How one step in each direction changes x and y. Moving in direction d means
// x += DX[d] and y += DY[d]. Screen y grows downwards, so up is -1.
//
// C lesson: "extern" declares that these arrays exist without creating them. They are
// defined exactly once, in maze.c, and every file that includes this header can use them.
extern const int DX[4];
extern const int DY[4];

// Fills the grid with a fresh copy of the level and returns how many pellets it has.
int maze_reset(void);

// False if a row of the level template has the wrong width, which would be a typo.
bool maze_template_is_valid(void);

// What is on a tile. Tiles outside the maze read as walls.
Cell maze_cell(int col, int row);

// If the tile has a pellet or power pellet, removes it. Returns what was there.
Cell maze_eat(int col, int row);

// Removes every pellet. The soak test uses this to finish levels quickly.
void maze_clear_pellets(void);

// True if a tile blocks movement. Outside the maze is a wall too, except along the
// tunnel row, which wraps around from one side to the other.
bool is_wall(int col, int row);

// The direction pointing the other way.
Dir opposite(Dir d);

// Which column or row a maze pixel coordinate falls in. Positions in the tunnel can be
// a little outside the maze, so these work for negative numbers too.
int tile_col(int x);
int tile_row(int y);

// True if (x, y) is exactly the center of a tile. Pac-Man and the ghosts can only turn
// there, which keeps them lined up with the corridors.
bool at_tile_center(int x, int y);

// Teleports through the tunnel: a position that went off one side of the maze comes
// back on the other side.
void wrap_x(int *x);

#endif
