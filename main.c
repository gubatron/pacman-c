// main.c
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#define WINDOW_WIDTH  816  // 34 tiles * 24 pixels per tile
#define WINDOW_HEIGHT 816
#define TILE_SIZE     24.0f
#define PLAYER_RADIUS 12.0f
#define SPEED         (PLAYER_RADIUS / 3.0f)
#define MOUTH_SPEED   10.0f // degrees per frame

typedef struct {
    float x;
    float y;
} Vec2;

typedef struct {
    clock_t start_time;
    double duration; // in seconds
} PacmanScent;

typedef struct {
    int col;
    int row;
} Tile;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
TTF_Font *font = NULL;

bool init();
void close_sdl();
void handle_keypress(Vec2 *player_direction, float *desired_angle);
void update_player_position(Vec2 *player_pos, Vec2 *player_direction);
void draw_circle(Vec2 center, float radius, float mouth_angle, Vec2 player_direction, float desired_angle);
void clear_background();
void draw_grid();
void render_player_position_hud(Vec2 player_pos);
Tile get_tile(Vec2 pos);
void update_pacman_mouth_angle(bool *mouth_opening, float *mouth_angle);
float angle_diff(float a, float b);

int main(int argc, char *argv[]) {
    if (!init()) {
        printf("Failed to initialize SDL.\n");
        return 1;
    }

    Vec2 player_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    Vec2 player_direction = { 0.0f, 0.0f };
    Vec2 last_direction = { 0.0f, 0.0f };

    float desired_angle = 0.0f; // Starting direction (right)
    float pacman_mouth_angle = 45.0f; // Degrees
    bool pacman_mouth_opening = false;

    bool quit = false;
    SDL_Event e;

    while (!quit) {
        // Event handling
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }

        handle_keypress(&player_direction, &desired_angle);
        update_player_position(&player_pos, &player_direction);

        // Screen wrapping
        if (player_pos.x < -PLAYER_RADIUS * 2) {
            player_pos.x = WINDOW_WIDTH + PLAYER_RADIUS;
        } else if (player_pos.x > WINDOW_WIDTH + PLAYER_RADIUS * 2) {
            player_pos.x = -PLAYER_RADIUS;
        }
        if (player_pos.y < -PLAYER_RADIUS * 2) {
            player_pos.y = WINDOW_HEIGHT + PLAYER_RADIUS;
        } else if (player_pos.y > WINDOW_HEIGHT + PLAYER_RADIUS * 2) {
            player_pos.y = -PLAYER_RADIUS;
        }

        // Clear screen
        clear_background();

        // Draw grid
        draw_grid();

        // Draw Pacman
        draw_circle(player_pos, PLAYER_RADIUS, pacman_mouth_angle, player_direction, desired_angle);

        // Update mouth angle
        update_pacman_mouth_angle(&pacman_mouth_opening, &pacman_mouth_angle);

        // Render HUD
        render_player_position_hud(player_pos);

        // Update screen
        SDL_RenderPresent(renderer);

        // Cap at ~60 FPS
        SDL_Delay(16);
    }

    close_sdl();
    return 0;
}

bool init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    // Initialize SDL_ttf
    if (TTF_Init() == -1) {
        printf("SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError());
        return false;
    }

    window = SDL_CreateWindow("Pacman", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);

    if (window == NULL) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED /*| SDL_RENDERER_PRESENTVSYNC*/);
    if (renderer == NULL) {
        printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        return false;
    }

    // Load font
    font = TTF_OpenFont("font.ttf", 24); // Ensure font.ttf is in the same directory
    if (font == NULL) {
        printf("Failed to load font! TTF_Error: %s\n", TTF_GetError());
        return false;
    }

    return true;
}

void close_sdl() {
    // Free font
    TTF_CloseFont(font);
    font = NULL;

    // Destroy renderer and window
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    window = NULL;
    renderer = NULL;

    // Quit SDL_ttf
    TTF_Quit();

    // Quit SDL
    SDL_Quit();
}


void handle_keypress(Vec2 *player_direction, float *desired_angle) {
    const Uint8 *keystate = SDL_GetKeyboardState(NULL);

    bool key_pressed = false;
    Vec2 new_direction = *player_direction;

    if (keystate[SDL_SCANCODE_W] || keystate[SDL_SCANCODE_UP]) {
        new_direction.x = 0.0f;
        new_direction.y = -1.0f;
        *desired_angle = -90.0f;
        key_pressed = true;
    } else if (keystate[SDL_SCANCODE_S] || keystate[SDL_SCANCODE_DOWN]) {
        new_direction.x = 0.0f;
        new_direction.y = 1.0f;
        *desired_angle = 90.0f;
        key_pressed = true;
    } else if (keystate[SDL_SCANCODE_A] || keystate[SDL_SCANCODE_LEFT]) {
        new_direction.x = -1.0f;
        new_direction.y = 0.0f;
        *desired_angle = 180.0f;
        key_pressed = true;
    } else if (keystate[SDL_SCANCODE_D] || keystate[SDL_SCANCODE_RIGHT]) {
        new_direction.x = 1.0f;
        new_direction.y = 0.0f;
        *desired_angle = 0.0f;
        key_pressed = true;
    }

    // Prevent diagonal movement
    if (new_direction.x != 0.0f && new_direction.y != 0.0f) {
        new_direction.y = 0.0f;
    }

    if (key_pressed) {
        // Update player_direction only when a key is pressed
        player_direction->x = new_direction.x;
        player_direction->y = new_direction.y;
    }
    // Else, keep moving in the current direction
}

void update_player_position(Vec2 *player_pos, Vec2 *player_direction) {
    player_pos->x += SPEED * player_direction->x;
    player_pos->y += SPEED * player_direction->y;
}

void draw_circle(Vec2 center, float radius, float mouth_angle, Vec2 player_direction, float desired_angle) {
    int r = (int)radius;
    int cx = (int)center.x;
    int cy = (int)center.y;

    for (int w = 0; w < r * 2; w++) {
        for (int h = 0; h < r * 2; h++) {
            int dx = w - r; // Horizontal offset from center
            int dy = h - r; // Vertical offset from center

            // Compute angle from positive x-axis to point (dx, dy)
            float dx_f = (float)dx;
            float dy_f = (float)dy;
            float angle = atan2f(dy_f, dx_f) * (180.0f / M_PI);
            float delta_angle = angle_diff(angle, desired_angle);

            // Check if the point is outside the mouth's opening
            if (dx * dx + dy * dy <= r * r && delta_angle > mouth_angle / 2.0f) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Yellow color
                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    }
}

void clear_background() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black background
    SDL_RenderClear(renderer);
}

void draw_grid() {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White lines

    // Vertical lines
    for (int x = 0; x <= WINDOW_WIDTH; x += (int)TILE_SIZE) {
        SDL_RenderDrawLine(renderer, x, 0, x, WINDOW_HEIGHT);
    }

    // Horizontal lines
    for (int y = 0; y <= WINDOW_HEIGHT; y += (int)TILE_SIZE) {
        SDL_RenderDrawLine(renderer, 0, y, WINDOW_WIDTH, y);
    }
}

void render_player_position_hud(Vec2 player_pos) {
    Tile tile = get_tile(player_pos);
    char tile_text[50];
    sprintf(tile_text, "Tile: (%02d, %02d)", tile.col, tile.row);

    char player_text[100];
    sprintf(player_text, "Pos: (%03d, %03d) %s", (int)player_pos.x, (int)player_pos.y, tile_text);

    SDL_Color textColor = { 255, 255, 255, 255 };
    SDL_Surface *textSurface = TTF_RenderText_Blended(font, player_text, textColor);
    if (textSurface != NULL) {
        SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
        if (textTexture != NULL) {
            int text_width = textSurface->w;
            int text_height = textSurface->h;
            SDL_Rect renderQuad = { WINDOW_WIDTH - text_width - 10, 10, text_width, text_height };

            SDL_RenderCopy(renderer, textTexture, NULL, &renderQuad);

            SDL_DestroyTexture(textTexture);
        }
        SDL_FreeSurface(textSurface);
    }
}

Tile get_tile(Vec2 pos) {
    Tile tile;
    tile.col = (int)(pos.x / TILE_SIZE);
    tile.row = (int)(pos.y / TILE_SIZE);
    return tile;
}

void update_pacman_mouth_angle(bool *mouth_opening, float *mouth_angle) {
    if (*mouth_opening) {
        *mouth_angle += MOUTH_SPEED;
        if (*mouth_angle >= 45.0f) {
            *mouth_angle = 45.0f;
            *mouth_opening = false;
        }
    } else {
        *mouth_angle -= MOUTH_SPEED;
        if (*mouth_angle <= 5.0f) {
            *mouth_angle = 5.0f;
            *mouth_opening = true;
        }
    }
}

float angle_diff(float a, float b) {
    float diff = a - b;
    while (diff < -180.0f) diff += 360.0f;
    while (diff > 180.0f) diff -= 360.0f;
    return fabsf(diff);
}
