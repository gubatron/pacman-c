// config.h
// The numbers that more than one part of the game needs: the size of the maze and the
// window, the frame rate, and where the ghost house and the fruit are.
//
// Constants that only one file uses live in that file instead, next to the code that
// uses them, so you never have to hunt for them.
//
// C lesson: header guards. A header can be included by several files, and indirectly
// more than once by the same file (game.h includes player.h, which includes maze.h,
// which includes this file). The #ifndef / #define / #endif guard below makes the
// compiler skip the contents the second time, so nothing is defined twice.
#ifndef PACMAN_CONFIG_H
#define PACMAN_CONFIG_H

// ---------------------------------------------------------------------------
// Maze and window
// ---------------------------------------------------------------------------

// The arcade maze is 28 tiles wide and 31 tiles tall.
#define MAZE_COLS 28
#define MAZE_ROWS 31

// Each tile is drawn as a TILE x TILE square of pixels. Every position in the game is
// measured in these "maze pixels", with (0, 0) at the top left corner of the maze.
#define TILE 24

// The score sits in a strip above the maze and the lives and level in a strip below it.
// Positions in the maze are drawn HUD_HEIGHT pixels lower on screen to make room.
#define HUD_HEIGHT    (TILE * 2)
#define WINDOW_WIDTH  (MAZE_COLS * TILE)                           // 672
#define WINDOW_HEIGHT (HUD_HEIGHT + MAZE_ROWS * TILE + HUD_HEIGHT) // 840

// The game logic runs exactly 60 times per second, like the arcade. All timers in the
// game count frames, so "2 * FPS" means two seconds.
#define FPS 60

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

#define NUM_GHOSTS       4
#define START_LIVES      3
#define EXTRA_LIFE_SCORE 10000

// Speeds are fractions of this: pixels per frame at "100%" speed, about 9.5 tiles per
// second. Pac-Man runs at 80% of it on the first level, for example.
#define BASE_SPEED 3.79f

// The row of the side tunnels. Leaving the maze on the left brings you back on the
// right, and ghosts slow down inside the tunnel.
#define TUNNEL_ROW 14

// ---------------------------------------------------------------------------
// Landmarks, in maze pixels
// ---------------------------------------------------------------------------

// The ghost house sits in the middle of the maze. Its door is between columns 13 and 14,
// so the center of the door is the boundary between those two tiles.
#define DOOR_X  (14 * TILE)
#define EXIT_Y  (11 * TILE + TILE / 2) // the corridor right above the door
#define HOUSE_Y (14 * TILE + TILE / 2) // the middle row inside the house

// Eaten ghosts (just their eyes) race back to this tile, right above the door, and then
// go in through the door.
#define EYES_TARGET_COL 13
#define EYES_TARGET_ROW 11

// Fruit appears below the ghost house.
#define FRUIT_X (14 * TILE)
#define FRUIT_Y (17 * TILE + TILE / 2)

#endif
