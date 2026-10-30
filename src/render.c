// render.c
// Everything that draws. Shapes are drawn with SDL's basic primitives (points, lines and
// filled rectangles) and text with SDL_ttf, so there are no image files.
//
// Coordinates: the game keeps positions in maze pixels, with y = 0 at the top of the
// maze. On screen the maze starts HUD_HEIGHT pixels down, below the score, so the drawing
// code adds HUD_HEIGHT to every y it takes from the game.
//
// SDL draws in two steps: everything is drawn into a hidden buffer, and SDL_RenderPresent
// shows the finished picture at once. That way you never see a half-drawn frame.
#include "render.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "sound.h"

#define PI_F          3.14159265f
#define PLAYER_RADIUS 11
#define GHOST_RADIUS  11

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static TTF_Font *font = NULL;

// Which way Pac-Man's mouth faces for each direction, as an angle in degrees. Angles
// grow clockwise on screen because y grows downwards.
static const float FACING_DEGREES[4] = { -90.0f, 180.0f, 90.0f, 0.0f }; // up, left, down, right

static const SDL_Color WHITE = { 255, 255, 255, 255 };
static const SDL_Color YELLOW = { 255, 255, 0, 255 };
static const SDL_Color RED = { 255, 0, 0, 255 };
static const SDL_Color PEACH = { 255, 184, 151, 255 }; // pellets, and the faces of frightened ghosts
static const SDL_Color PINK = { 255, 184, 255, 255 };  // the ghost house door
static const SDL_Color BLUE = { 33, 33, 255, 255 };    // frightened ghosts, and pupils
static const SDL_Color WALL_BLUE = { 33, 33, 222, 255 };

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool render_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() == -1) {
        printf("SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError());
        return false;
    }

    window = SDL_CreateWindow("Pacman", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_SHOWN);
    if (window == NULL) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    // Ask for the graphics card, synchronized with the display's refresh (VSYNC) so the
    // picture never tears. Without a graphics card, fall back to drawing in software.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == NULL) {
        printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        return false;
    }
    // Let colors with an alpha below 255 be see-through, for the speed trail. Everything
    // else is drawn fully opaque, so it looks the same either way.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Load the font from next to the executable, so the game works wherever it is
    // started from, and then from the current folder
    char font_path[1024] = "font.ttf";
    char *base_path = SDL_GetBasePath();
    if (base_path != NULL) {
        snprintf(font_path, sizeof(font_path), "%sfont.ttf", base_path);
        SDL_free(base_path);
    }
    font = TTF_OpenFont(font_path, 20);
    if (font == NULL) {
        font = TTF_OpenFont("font.ttf", 20);
    }
    if (font == NULL) {
        printf("Failed to load font! TTF_Error: %s\n", TTF_GetError());
        return false;
    }
    return true;
}

void render_quit(void) {
    // Free things in the reverse order they were created
    TTF_CloseFont(font);
    font = NULL;
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
    SDL_DestroyWindow(window);
    window = NULL;
    TTF_Quit();
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

static void set_color(SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

// A filled circle, drawn as one horizontal line per row. For the row dy away from the
// center, Pythagoras gives the half-width: sqrt(r^2 - dy^2).
static void fill_circle(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++) {
        int half = (int)sqrtf((float)(r * r - dy * dy));
        SDL_RenderDrawLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
    }
}

// Horizontal and vertical lines two pixels thick, for the maze walls.
static void hline(int x1, int x2, int y) {
    SDL_Rect r = { x1 < x2 ? x1 : x2, y - 1, abs(x2 - x1) + 1, 2 };
    SDL_RenderFillRect(renderer, &r);
}

static void vline(int x, int y1, int y2) {
    SDL_Rect r = { x - 1, y1 < y2 ? y1 : y2, 2, abs(y2 - y1) + 1 };
    SDL_RenderFillRect(renderer, &r);
}

// SDL_ttf turns text into an image (a "surface") in main memory. The renderer can only
// draw textures, which live on the graphics card, so we convert it, draw it, and free
// both. Simple, though not the fastest; a bigger game would cache the textures.
static void draw_text(const char *text, int x, int y, SDL_Color color, bool centered) {
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);
    if (surface == NULL) {
        return;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture != NULL) {
        SDL_Rect quad = { centered ? x - surface->w / 2 : x, y, surface->w, surface->h };
        SDL_RenderCopy(renderer, texture, NULL, &quad);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

// How far apart two angles are, in degrees from 0 to 180, going the short way around.
static float angle_diff(float a, float b) {
    float diff = a - b;
    while (diff < -180.0f) diff += 360.0f;
    while (diff > 180.0f) diff -= 360.0f;
    return fabsf(diff);
}

// ---------------------------------------------------------------------------
// Characters and fruit
// ---------------------------------------------------------------------------

// Pac-Man is a circle with a wedge cut out for the mouth. We test every pixel in the
// circle and keep those whose angle from the center is more than half the mouth's
// opening away from the direction he faces. A mouth of 360 degrees or more leaves
// nothing, which the death animation uses to make him vanish.
static void draw_pacman(int cx, int cy, int radius, float mouth_angle, float facing) {
    // Collected in an array and drawn with one call, which is much faster than one call
    // per pixel. Big enough for a radius of up to 16.
    static SDL_Point points[(2 * 16 + 1) * (2 * 16 + 1)];
    int count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) {
                continue; // outside the circle
            }
            float angle = atan2f((float)dy, (float)dx) * (180.0f / PI_F);
            if (angle_diff(angle, facing) > mouth_angle / 2.0f) {
                points[count].x = cx + dx;
                points[count].y = cy + dy;
                count++;
            }
        }
    }
    set_color(YELLOW);
    SDL_RenderDrawPoints(renderer, points, count);
}

// When Pac-Man is fast, faint copies of him trail behind: every other remembered
// position, each smaller and fainter than the one before. Alpha is how opaque a color
// is, from 0 (invisible) to 255 (solid); with blending on, a low alpha lets the maze
// show through.
static void draw_trail(void) {
    float strength = player_trail_strength();
    if (strength <= 0.0f) {
        return;
    }
    for (int i = 1; i < TRAIL_LENGTH; i += 2) {
        int x = game.pac.trail[i].x;
        int y = game.pac.trail[i].y + HUD_HEIGHT;
        // Skip points from the other side of the tunnel, so the trail doesn't jump across
        if (abs(x - game.pac.x) > TRAIL_LENGTH * TILE / 2) {
            continue;
        }
        float fade = 1.0f - (float)(i + 1) / (TRAIL_LENGTH + 1); // older points are fainter
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, (Uint8)(110.0f * fade * strength));
        fill_circle(x, y, PLAYER_RADIUS - i / 2);
    }
}

static void draw_ghost(const Ghost *g) {
    int cx = g->x;
    int cy = g->y + HUD_HEIGHT;
    int r = GHOST_RADIUS;
    bool eyes_only = g->state == GHOST_EYES || g->state == GHOST_ENTERING;

    if (!eyes_only) {
        SDL_Color body = g->color;
        bool flash = false;
        if (g->frightened) {
            // Blue, flashing white in the last two seconds as a warning
            flash = game.fright_timer < 2 * FPS && (game.frame / 10) % 2 == 0;
            body = flash ? WHITE : BLUE;
        }
        set_color(body);
        // The top half of a circle for the dome...
        for (int dy = -r; dy <= 0; dy++) {
            int half = (int)sqrtf((float)(r * r - dy * dy));
            SDL_RenderDrawLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
        }
        // ...a rectangle for the body...
        SDL_Rect rect = { cx - r, cy, 2 * r + 1, r - 3 };
        SDL_RenderFillRect(renderer, &rect);
        // ...and a skirt of little feet that shift every 8 frames, so it seems to walk
        int phase = (game.frame / 8) % 2;
        for (int x = -r; x <= r; x++) {
            int foot = ((x + r + phase * 3) / 4) % 2 == 0 ? 4 : 0;
            SDL_RenderDrawLine(renderer, cx + x, cy + r - 3, cx + x, cy + r - 3 + foot);
        }

        if (g->frightened) {
            // A frightened face: two small eyes and a wavy mouth
            set_color(flash ? RED : PEACH);
            SDL_Rect left_eye = { cx - 5, cy - 5, 3, 3 };
            SDL_Rect right_eye = { cx + 3, cy - 5, 3, 3 };
            SDL_RenderFillRect(renderer, &left_eye);
            SDL_RenderFillRect(renderer, &right_eye);
            for (int i = 0; i < 6; i++) {
                int x1 = cx - 8 + i * 3;
                int y1 = cy + 4 + (i % 2 == 0 ? 0 : -2);
                int y2 = cy + 4 + (i % 2 == 0 ? -2 : 0);
                SDL_RenderDrawLine(renderer, x1, y1, x1 + 3, y2);
            }
            return;
        }
    }

    // Eyes that look where the ghost is heading: the whites move a little in that
    // direction and the pupils twice as much
    int ex = g->dir == DIR_NONE ? 0 : DX[g->dir] * 2;
    int ey = g->dir == DIR_NONE ? 0 : DY[g->dir] * 2;
    set_color(WHITE);
    fill_circle(cx - 5 + ex / 2, cy - 3 + ey / 2, 4);
    fill_circle(cx + 5 + ex / 2, cy - 3 + ey / 2, 4);
    set_color(BLUE);
    fill_circle(cx - 5 + ex, cy - 3 + ey, 2);
    fill_circle(cx + 5 + ex, cy - 3 + ey, 2);
}

static void draw_cherry(int cx, int cy) {
    SDL_SetRenderDrawColor(renderer, 222, 151, 81, 255); // stems
    SDL_RenderDrawLine(renderer, cx - 5, cy + 2, cx + 5, cy - 9);
    SDL_RenderDrawLine(renderer, cx + 5, cy + 1, cx + 5, cy - 9);
    set_color(RED);
    fill_circle(cx - 5, cy + 4, 5);
    fill_circle(cx + 5, cy + 3, 5);
}

// ---------------------------------------------------------------------------
// The maze
// ---------------------------------------------------------------------------

// For drawing, the door counts as a wall too, and so does everything outside the maze,
// including the tunnel, so the tunnel's walls get drawn.
static bool wall_for_drawing(int col, int row) {
    Cell c = maze_cell(col, row);
    return c == CELL_WALL || c == CELL_DOOR;
}

// Walls are drawn as outlines, like the arcade. Instead of filling wall tiles, we draw a
// line along each side of a wall tile that faces an open tile, pulled in by `inset`
// pixels. Where two such lines meet they join up into corners, so the outlines of whole
// blocks come out of simple per-tile rules.
static void draw_maze(SDL_Color color) {
    const int inset = TILE / 3;
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            int left = col * TILE;
            int top = HUD_HEIGHT + row * TILE;
            int right = left + TILE;
            int bottom = top + TILE;

            Cell cell = maze_cell(col, row);
            if (cell == CELL_DOOR) {
                set_color(PINK);
                SDL_Rect door = { left, top + TILE / 2 - 2, TILE, 4 };
                SDL_RenderFillRect(renderer, &door);
                continue;
            }
            if (cell != CELL_WALL) {
                continue;
            }
            set_color(color);

            bool open_up = !wall_for_drawing(col, row - 1);
            bool open_down = !wall_for_drawing(col, row + 1);
            bool open_left = !wall_for_drawing(col - 1, row);
            bool open_right = !wall_for_drawing(col + 1, row);

            // A line along each open side. Lines stop at the inset where they meet
            // another open side, which makes an outside corner.
            if (open_up) {
                hline(open_left ? left + inset : left, open_right ? right - inset : right, top + inset);
            }
            if (open_down) {
                hline(open_left ? left + inset : left, open_right ? right - inset : right, bottom - inset);
            }
            if (open_left) {
                vline(left + inset, open_up ? top + inset : top, open_down ? bottom - inset : bottom);
            }
            if (open_right) {
                vline(right - inset, open_up ? top + inset : top, open_down ? bottom - inset : bottom);
            }

            // Inside corners: both neighbors are walls but the tile between them, on the
            // diagonal, is open. Draw a small L to round that corner off.
            for (int sy = -1; sy <= 1; sy += 2) {
                for (int sx = -1; sx <= 1; sx += 2) {
                    if (wall_for_drawing(col + sx, row) && wall_for_drawing(col, row + sy) &&
                        !wall_for_drawing(col + sx, row + sy)) {
                        int px = sx > 0 ? right - inset : left + inset;
                        int py = sy > 0 ? bottom - inset : top + inset;
                        hline(px, sx > 0 ? right : left, py);
                        vline(px, py, sy > 0 ? bottom : top);
                    }
                }
            }
        }
    }
}

static void draw_pellets(void) {
    // Power pellets blink during play: shown for 10 frames, hidden for 10
    bool show_power = game.state != STATE_PLAYING || (game.frame / 10) % 2 == 0;
    set_color(PEACH);
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            int cx = col * TILE + TILE / 2;
            int cy = HUD_HEIGHT + row * TILE + TILE / 2;
            Cell cell = maze_cell(col, row);
            if (cell == CELL_PELLET) {
                SDL_Rect dot = { cx - 2, cy - 2, 4, 4 };
                SDL_RenderFillRect(renderer, &dot);
            } else if (cell == CELL_POWER_PELLET && show_power) {
                fill_circle(cx, cy, 7);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Text and the whole frame
// ---------------------------------------------------------------------------

// A bar showing Pac-Man's speed, between the spare lives and the level. It fills up
// to his top speed, and a white tick marks the arcade's full speed (100%), which the
// pellet bonus can take him past.
static void draw_speed_gauge(int bottom) {
    const int label_x = 220;   // right after room for six spare lives
    const int bar_x = 328;     // after the 100 px wide "SPEED" label and a gap
    const int bar_w = 110;     // ends well before the cherry next to "LEVEL 10"
    const int bar_y = bottom + 16;
    const int bar_h = 12;

    draw_text("SPEED", label_x, bottom + 12, WHITE, false);

    float max_speed = player_max_speed();
    int fill = (int)((float)bar_w * player_speed() / max_speed);
    if (fill > bar_w) {
        fill = bar_w;
    }
    set_color(YELLOW);
    SDL_Rect filled = { bar_x, bar_y, fill, bar_h };
    SDL_RenderFillRect(renderer, &filled);

    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
    SDL_Rect outline = { bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2 };
    SDL_RenderDrawRect(renderer, &outline);

    int tick_x = bar_x + (int)((float)bar_w * BASE_SPEED / max_speed);
    set_color(WHITE);
    SDL_Rect tick = { tick_x - 1, bar_y - 3, 2, bar_h + 6 };
    SDL_RenderFillRect(renderer, &tick);
}

static void draw_hud(void) {
    char text[64];

    // Score and high score at the top
    draw_text("1UP", 3 * TILE, 2, WHITE, false);
    snprintf(text, sizeof(text), "%d", game.score);
    draw_text(text, 3 * TILE, 24, WHITE, false);
    draw_text("HIGH SCORE", WINDOW_WIDTH / 2, 2, WHITE, true);
    snprintf(text, sizeof(text), "%d", game.high_score);
    draw_text(text, WINDOW_WIDTH / 2, 24, WHITE, true);
    if (sound_is_muted()) {
        // Top right, right-aligned by measuring the text first
        const char *muted = "MUTED (M)";
        int muted_width = 0;
        TTF_SizeText(font, muted, &muted_width, NULL);
        draw_text(muted, WINDOW_WIDTH - TILE - muted_width, 2, (SDL_Color){ 128, 128, 128, 255 }, false);
    }

    // Spare lives at the bottom left (the life being played isn't shown), the speed
    // gauge in the middle, and the level at the bottom right
    int bottom = HUD_HEIGHT + MAZE_ROWS * TILE;
    for (int i = 0; i < game.lives - 1 && i < 6; i++) {
        draw_pacman(2 * TILE + i * 30, bottom + TILE, 9, 60.0f, 180.0f);
    }
    snprintf(text, sizeof(text), "LEVEL %d", game.level);
    int text_width = 0;
    TTF_SizeText(font, text, &text_width, NULL);
    int text_x = WINDOW_WIDTH - TILE - text_width;
    draw_text(text, text_x, bottom + 12, WHITE, false);
    draw_cherry(text_x - TILE, bottom + TILE);
    draw_speed_gauge(bottom);
}

void render_frame(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Back to front: maze, pellets and fruit, then the characters, then text on top

    // When a level is cleared the walls flash white during the last two seconds
    SDL_Color wall_color = WALL_BLUE;
    if (game.state == STATE_LEVEL_DONE && game.state_timer < 2 * FPS && (game.state_timer / 15) % 2 == 0) {
        wall_color = WHITE;
    }
    draw_maze(wall_color);
    draw_pellets();

    if (game.fruit_timer > 0) {
        draw_cherry(FRUIT_X, HUD_HEIGHT + FRUIT_Y);
    }
    if (game.state == STATE_PLAYING) {
        draw_trail();
    }

    // When Pac-Man is caught everything freezes for half a second, then the ghosts
    // disappear and the death animation plays for the remaining 1.5 seconds
    bool dying_animation = game.state == STATE_DYING && game.state_timer <= 3 * FPS / 2;
    bool show_ghosts = game.state != STATE_LEVEL_DONE && game.state != STATE_GAME_OVER && !dying_animation;
    if (show_ghosts) {
        for (int i = 0; i < NUM_GHOSTS; i++) {
            draw_ghost(&game.ghosts[i]);
        }
    }

    int pac_y = game.pac.y + HUD_HEIGHT;
    if (dying_animation) {
        // The mouth opens all the way around, facing up, until Pac-Man is gone
        float progress = 1.0f - (float)game.state_timer / (3 * FPS / 2);
        draw_pacman(game.pac.x, pac_y, PLAYER_RADIUS, progress * 380.0f, -90.0f);
    } else if (game.state == STATE_READY || game.state == STATE_LEVEL_DONE) {
        draw_pacman(game.pac.x, pac_y, PLAYER_RADIUS, 0.0f, 0.0f); // a full circle
    } else if (game.state != STATE_GAME_OVER) {
        draw_pacman(game.pac.x, pac_y, PLAYER_RADIUS, game.pac.mouth_angle, FACING_DEGREES[game.pac.dir]);
    }

    if (game.popup_timer > 0) {
        char text[16];
        snprintf(text, sizeof(text), "%d", game.popup_points);
        draw_text(text, game.popup_x, HUD_HEIGHT + game.popup_y - 12, (SDL_Color){ 0, 255, 255, 255 }, true);
    }

    // Messages go in the empty row below the ghost house, like the arcade
    int message_y = HUD_HEIGHT + 17 * TILE;
    if (game.state == STATE_READY) {
        draw_text("READY!", WINDOW_WIDTH / 2, message_y, YELLOW, true);
    } else if (game.state == STATE_GAME_OVER) {
        draw_text("GAME  OVER", WINDOW_WIDTH / 2, message_y, RED, true);
        draw_text("PRESS ENTER TO PLAY AGAIN", WINDOW_WIDTH / 2, HUD_HEIGHT + 20 * TILE, WHITE, true);
    } else if (game.paused) {
        draw_text("PAUSED", WINDOW_WIDTH / 2, message_y, YELLOW, true);
    }

    draw_hud();

    SDL_RenderPresent(renderer); // show the finished frame
}

bool render_save_bmp(const char *path) {
    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_WIDTH, WINDOW_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    if (shot == NULL) {
        return false;
    }
    bool ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch) == 0 &&
              SDL_SaveBMP(shot, path) == 0;
    SDL_FreeSurface(shot);
    return ok;
}
