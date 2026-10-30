// maze.c
// The level layout and the tile geometry described in maze.h.
#include "maze.h"

#include <string.h>

// The level, drawn with the Cell characters from maze.h:
// '#' wall, '.' pellet, 'o' power pellet, '-' ghost house door, ' ' empty.
//
// Row 14 is open at both ends: that is the tunnel. The empty middle is the ghost house,
// and the gap two rows below the house is where Pac-Man starts.
static const char *const LEVEL[MAZE_ROWS] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "     #.##          ##.#     ",
    "     #.## ###--### ##.#     ",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "     #.## ######## ##.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#o..##.......  .......##..o#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
};

// The grid being played. LEVEL never changes; this copy loses its pellets as they are
// eaten and is refilled from LEVEL at the start of every level.
//
// C lesson: "static" at file level means private to this file. No other file can touch
// `grid` directly, even by declaring it extern. The +1 leaves room for the '\0' that
// ends each row, because each row is copied from a C string.
static char grid[MAZE_ROWS][MAZE_COLS + 1];

const int DX[4] = { 0, -1, 0, 1 };  // up, left, down, right
const int DY[4] = { -1, 0, 1, 0 };

static bool in_bounds(int col, int row) {
    return col >= 0 && col < MAZE_COLS && row >= 0 && row < MAZE_ROWS;
}

static bool is_pellet(char c) {
    return c == CELL_PELLET || c == CELL_POWER_PELLET;
}

bool maze_template_is_valid(void) {
    for (int row = 0; row < MAZE_ROWS; row++) {
        if (strlen(LEVEL[row]) != MAZE_COLS) {
            return false;
        }
    }
    return true;
}

int maze_reset(void) {
    int pellets = 0;
    for (int row = 0; row < MAZE_ROWS; row++) {
        memcpy(grid[row], LEVEL[row], MAZE_COLS + 1); // the row and its '\0'
        for (int col = 0; col < MAZE_COLS; col++) {
            if (is_pellet(grid[row][col])) {
                pellets++;
            }
        }
    }
    return pellets;
}

Cell maze_cell(int col, int row) {
    return in_bounds(col, row) ? (Cell)grid[row][col] : CELL_WALL;
}

Cell maze_eat(int col, int row) {
    if (!in_bounds(col, row)) {
        return CELL_WALL;
    }
    Cell eaten = (Cell)grid[row][col];
    if (is_pellet(eaten)) {
        grid[row][col] = CELL_EMPTY;
    }
    return eaten;
}

void maze_clear_pellets(void) {
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            if (is_pellet(grid[row][col])) {
                grid[row][col] = CELL_EMPTY;
            }
        }
    }
}

bool is_wall(int col, int row) {
    if (row < 0 || row >= MAZE_ROWS) {
        return true;
    }
    if (col < 0 || col >= MAZE_COLS) {
        return row != TUNNEL_ROW; // the tunnel continues past the edges
    }
    char c = grid[row][col];
    return c == CELL_WALL || c == CELL_DOOR;
}

Dir opposite(Dir d) {
    // Thanks to the order of the Dir enum, opposites are two apart: UP(0) <-> DOWN(2),
    // LEFT(1) <-> RIGHT(3).
    return d == DIR_NONE ? DIR_NONE : (Dir)((d + 2) % 4);
}

// Division that rounds down instead of towards zero.
//
// C lesson: in C, integer division truncates towards zero, so -1 / 24 is 0. That would
// put x = -1 (just left of the maze, in the tunnel) in column 0. Rounding down gives
// column -1, which is what we want.
static int floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int tile_col(int x) {
    return floor_div(x, TILE);
}

int tile_row(int y) {
    return floor_div(y, TILE);
}

bool at_tile_center(int x, int y) {
    // Tile centers are at TILE / 2, TILE + TILE / 2, ... The "% TILE + TILE) % TILE" dance
    // keeps the remainder positive for negative coordinates, where C's % would give a
    // negative result.
    return ((x - TILE / 2) % TILE + TILE) % TILE == 0 && ((y - TILE / 2) % TILE + TILE) % TILE == 0;
}

void wrap_x(int *x) {
    // Positions may run one tile past either edge (columns -1 and 28) so that sprites
    // slide out of view before reappearing on the other side.
    const int span = (MAZE_COLS + 2) * TILE;
    if (*x < -TILE) {
        *x += span;
    } else if (*x >= MAZE_COLS * TILE + TILE) {
        *x -= span;
    }
}
