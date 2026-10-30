// main.c
// Pac-Man in C with SDL2: the classic maze, pellets and power pellets, four ghosts
// with their chase/scatter personalities, fruit, lives, levels, sound and a saved high score.
// Controls: arrows or WASD to move, P to pause, M to mute, Enter to play again, Esc to quit.
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAZE_COLS     28
#define MAZE_ROWS     31
#define TILE          24
#define HUD_HEIGHT    (TILE * 2)
#define WINDOW_WIDTH  (MAZE_COLS * TILE)                           // 672
#define WINDOW_HEIGHT (HUD_HEIGHT + MAZE_ROWS * TILE + HUD_HEIGHT) // 840
#define FPS           60
#define PI_F          3.14159265f

#define PLAYER_RADIUS    11
#define GHOST_RADIUS     11
#define MOUTH_SPEED      8.0f // degrees per frame
#define NUM_GHOSTS       4
#define START_LIVES      3
#define EXTRA_LIFE_SCORE 10000
#define TUNNEL_ROW       14
#define BASE_SPEED       3.79f // pixels per frame at "100%" speed, about 9.5 tiles per second

// The ghost house sits in the middle of the maze; its door is between columns 13 and 14.
#define DOOR_X          (14 * TILE)
#define EXIT_Y          (11 * TILE + TILE / 2) // the corridor right above the door
#define HOUSE_Y         (14 * TILE + TILE / 2)
#define EYES_TARGET_COL 13
#define EYES_TARGET_ROW 11
#define FRUIT_X         (14 * TILE)
#define FRUIT_Y         (17 * TILE + TILE / 2)

// '#' wall, '.' pellet, 'o' power pellet, '-' ghost house door, ' ' empty
static const char *MAZE_TEMPLATE[MAZE_ROWS] = {
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

// Directions are ordered the way the arcade breaks ties between equally good turns.
typedef enum { DIR_NONE = -1, DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT } Dir;
static const int DX[4] = { 0, -1, 0, 1 };
static const int DY[4] = { -1, 0, 1, 0 };
static const float FACING_DEGREES[4] = { -90.0f, 180.0f, 90.0f, 0.0f };

typedef struct {
    int x, y; // center, in maze pixels
    Dir dir;
    Dir next_dir; // buffered turn, taken at the next tile center where it is open
    float speed_acc;
    bool moving;
} Pacman;

typedef enum { GHOST_IN_HOUSE, GHOST_LEAVING, GHOST_ACTIVE, GHOST_EYES, GHOST_ENTERING } GhostState;

typedef struct {
    int id; // 0 Blinky, 1 Pinky, 2 Inky, 3 Clyde
    SDL_Color color;
    int x, y;
    Dir dir;
    GhostState state;
    bool frightened;
    float speed_acc;
    int home_x;
    int scatter_col, scatter_row;
} Ghost;

typedef enum { STATE_READY, STATE_PLAYING, STATE_DYING, STATE_LEVEL_DONE, STATE_GAME_OVER } GameState;

typedef struct {
    char maze[MAZE_ROWS][MAZE_COLS + 1];
    Pacman pac;
    Ghost ghosts[NUM_GHOSTS];
    GameState state;
    int state_timer; // frames left in the current state
    int frame;
    int score, high_score, lives, level;
    bool extra_life_awarded;
    int dots_left, dots_eaten_level, dots_eaten_life, no_dot_timer;
    int mode_index, mode_timer; // scatter/chase schedule
    bool chase;
    int fright_timer, ghosts_eaten_combo, freeze_timer;
    int fruit_timer;
    int popup_timer, popup_x, popup_y, popup_points;
    float mouth_angle;
    bool mouth_opening;
    bool paused;
} Game;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
TTF_Font *font = NULL;
static Game game;
static char high_score_path[1024];

bool init();
void close_sdl();
static void new_game(void);
static void update(void);
static void render(void);
static void handle_keydown(SDL_Scancode key, bool *quit);
static void load_high_score(void);
static void save_high_score(void);
static void init_audio(void);
float angle_diff(float a, float b);
#ifdef PACMAN_DEBUG
static int run_debug(int frames, const char *screenshot_path);
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    for (int row = 0; row < MAZE_ROWS; row++) {
        if (strlen(MAZE_TEMPLATE[row]) != MAZE_COLS) {
            printf("Maze row %d is %zu columns wide, expected %d.\n", row, strlen(MAZE_TEMPLATE[row]), MAZE_COLS);
            return 1;
        }
    }

    if (!init()) {
        printf("Failed to initialize SDL.\n");
        return 1;
    }
    init_audio();
    srand((unsigned)time(NULL));
    load_high_score();

#ifdef PACMAN_DEBUG
    const char *frames_env = getenv("PACMAN_FRAMES");
    if (frames_env != NULL) {
        int result = run_debug(atoi(frames_env), getenv("PACMAN_SHOT"));
        close_sdl();
        return result;
    }
#endif

    new_game();

    // Fixed 60 Hz game logic, rendering as fast as the display refreshes.
    const double step = 1.0 / FPS;
    double accumulator = 0.0;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 previous = SDL_GetPerformanceCounter();
    bool quit = false;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                handle_keydown(e.key.keysym.scancode, &quit);
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        accumulator += (double)(now - previous) / (double)freq;
        previous = now;
        if (accumulator > 0.25) {
            accumulator = 0.25; // don't spiral after a stall
        }
        while (accumulator >= step) {
            update();
            accumulator -= step;
        }

        render();
        SDL_Delay(1);
    }

    save_high_score();
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

    // Create renderer, falling back to software rendering when there is no GPU
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == NULL) {
        printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        return false;
    }

    // Load the font from next to the executable, then from the current directory
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

static void close_audio(void);

void close_sdl() {
    close_audio();

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

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

// Every sound is synthesized while it plays, a bit like the arcade's wavetable chip, so
// there are no sound files. Without an audio device the game simply plays silently.

#define AUDIO_RATE    44100
#define NUM_FX_VOICES 3
#define FADE_STEP     (1.0f / (0.01f * AUDIO_RATE)) // 10 ms fades for loops, mute and pause
#define INTRO_UNIT    0.061                         // seconds per sixteenth; 64 of them fit READY!

typedef enum {
    SOUND_NONE,
    SOUND_INTRO,
    SOUND_WAKA,
    SOUND_GHOST_EATEN,
    SOUND_FRUIT,
    SOUND_EXTRA_LIFE,
    SOUND_DEATH,
    SOUND_SIREN, // loops from here on
    SOUND_FRIGHT,
    SOUND_EYES,
} Sound;

typedef struct {
    Sound sound;
    int pos;    // samples played so far
    int length; // in samples; 0 for loops
    int half;   // which half of the waka
    double phase, bass_phase;
} Voice;

// Shared with the audio thread: change it only while the device is locked.
typedef struct {
    SDL_AudioDeviceID device; // 0 when there is no audio
    Voice chomp;
    Voice fx[NUM_FX_VOICES];
    Voice loop;         // siren, frightened or eyes: one at a time
    Sound next_loop;    // the loop to switch to once the current one has faded out
    float loop_gain;
    float siren_pitch;  // 0 with a full maze, 1 near the last pellet
    float master_gain;
    bool muted, paused; // pause also freezes sounds mid-way
    int next_waka_half;
} Audio;

static Audio audio;

// The intro jingle as MIDI notes and lengths in sixteenths, melody over bass.
static const struct {
    int note, units;
} INTRO_MELODY[] = {
    { 71, 2 }, { 83, 2 }, { 78, 2 }, { 75, 2 }, { 83, 1 }, { 78, 3 }, { 75, 4 },
    { 72, 2 }, { 84, 2 }, { 79, 2 }, { 76, 2 }, { 84, 1 }, { 79, 3 }, { 76, 4 },
    { 71, 2 }, { 83, 2 }, { 78, 2 }, { 75, 2 }, { 83, 1 }, { 78, 3 }, { 75, 4 },
    { 75, 1 }, { 76, 1 }, { 77, 2 }, { 77, 1 }, { 78, 1 }, { 79, 2 }, { 79, 1 }, { 80, 1 }, { 81, 2 }, { 83, 4 },
};
static const int INTRO_BASS[32] = { // eighths
    47, 59, 47, 59, 47, 59, 47, 59, 48, 60, 48, 60, 48, 60, 48, 60,
    47, 59, 47, 59, 47, 59, 47, 59, 54, 54, 56, 56, 58, 58, 59, 59,
};

static double note_freq(int note) {
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

static float triangle(double phase) {
    double f = phase - floor(phase);
    return (float)(f < 0.5 ? 4.0 * f - 1.0 : 3.0 - 4.0 * f);
}

// Short attack and release so nothing clicks.
static float envelope(double t, double length) {
    double e = fmin(t / 0.004, (length - t) / 0.012);
    return e >= 1.0 ? 1.0f : (e <= 0.0 ? 0.0f : (float)e);
}

static float tone(Voice *v, double freq) {
    v->phase += freq / AUDIO_RATE;
    return triangle(v->phase);
}

static float sound_sample(Voice *v, double t) {
    switch (v->sound) {
    case SOUND_INTRO: {
        double units = t / INTRO_UNIT;
        double start = 0.0;
        float out = 0.0f;
        for (size_t i = 0; i < sizeof(INTRO_MELODY) / sizeof(INTRO_MELODY[0]); i++) {
            if (units < start + INTRO_MELODY[i].units) {
                double note_t = (units - start) * INTRO_UNIT;
                out = 0.2f * tone(v, note_freq(INTRO_MELODY[i].note)) * envelope(note_t, INTRO_MELODY[i].units * INTRO_UNIT);
                break;
            }
            start += INTRO_MELODY[i].units;
        }
        int eighth = (int)(units / 2.0);
        if (eighth < 32) {
            v->bass_phase += note_freq(INTRO_BASS[eighth]) / AUDIO_RATE;
            out += 0.16f * triangle(v->bass_phase) * envelope(t - eighth * 2 * INTRO_UNIT, 2 * INTRO_UNIT);
        }
        return out;
    }
    case SOUND_WAKA: { // "wa" sweeps down, "ka" sweeps back up
        double x = t / 0.11;
        double freq = v->half == 0 ? 600.0 - 350.0 * x : 250.0 + 350.0 * x;
        return 0.22f * tone(v, freq);
    }
    case SOUND_GHOST_EATEN: // a fast rising sweep while the game freezes
        return 0.18f * tone(v, 150.0 * pow(12.0, t / 0.5));
    case SOUND_FRUIT:
        return 0.2f * tone(v, 300.0 + 700.0 * fabs(cos(t / 0.2 * M_PI)));
    case SOUND_EXTRA_LIFE: { // a bell ringing eight times
        double beep_t = fmod(t, 0.125);
        return 0.18f * tone(v, 1568.0) * envelope(beep_t, 0.1) * (float)exp(-beep_t * 20.0);
    }
    case SOUND_DEATH:
        if (t < 1.1) { // "wah-wah": falling bends, each starting lower than the last
            double base = 900.0 * pow(150.0 / 900.0, t / 1.1);
            double bend = fmod(t, 0.11) / 0.11;
            return 0.22f * tone(v, base * (1.35 - 0.35 * bend)) * envelope(t, 1.1);
        }
        for (int i = 0; i < 2; i++) { // then two pops
            double pop_t = t - 1.2 - i * 0.15;
            if (pop_t >= 0.0 && pop_t < 0.09) {
                return 0.22f * tone(v, 200.0 + 1000.0 * pop_t / 0.09) * envelope(pop_t, 0.09);
            }
        }
        return 0.0f;
    case SOUND_SIREN: { // rises and falls, higher the fewer pellets are left
        double base = 380.0 + 260.0 * audio.siren_pitch;
        v->phase += base * (1.0 + 0.35 * (triangle(t / 0.42) + 1.0) / 2.0) / AUDIO_RATE;
        return 0.1f * sinf((float)(2.0 * M_PI * v->phase));
    }
    case SOUND_FRIGHT:
        return 0.11f * tone(v, 250.0 * pow(4.0, fmod(t, 0.135) / 0.135));
    case SOUND_EYES:
        return 0.09f * tone(v, 1500.0 * pow(0.4, fmod(t, 0.1) / 0.1));
    case SOUND_NONE:
        break;
    }
    return 0.0f;
}

static float voice_sample(Voice *v) {
    if (v->sound == SOUND_NONE) {
        return 0.0f;
    }
    double t = (double)v->pos / AUDIO_RATE;
    float out = sound_sample(v, t);
    v->pos++;
    if (v->length > 0) {
        out *= envelope(t, (double)v->length / AUDIO_RATE);
        if (v->pos >= v->length) {
            v->sound = SOUND_NONE;
        }
    }
    return out;
}

static void start_voice(Voice *v, Sound sound) {
    static const double SECONDS[] = {
        [SOUND_INTRO] = 64 * INTRO_UNIT + 0.05, [SOUND_WAKA] = 0.11, [SOUND_GHOST_EATEN] = 0.5,
        [SOUND_FRUIT] = 0.4, [SOUND_EXTRA_LIFE] = 1.0, [SOUND_DEATH] = 1.45,
    };
    v->sound = sound;
    v->pos = 0;
    v->length = sound < SOUND_SIREN ? (int)(SECONDS[sound] * AUDIO_RATE) : 0;
    v->phase = 0.0;
    v->bass_phase = 0.0;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    float *out = (float *)stream;
    int samples = len / (int)sizeof(float);
    float target = audio.muted || audio.paused ? 0.0f : 1.0f;

    for (int i = 0; i < samples; i++) {
        if (audio.master_gain < target) {
            audio.master_gain = fminf(target, audio.master_gain + FADE_STEP);
        } else if (audio.master_gain > target) {
            audio.master_gain = fmaxf(target, audio.master_gain - FADE_STEP);
        }
        if (audio.paused && audio.master_gain == 0.0f) {
            out[i] = 0.0f; // hold every sound where it was
            continue;
        }

        // A new loop waits for the current one to fade out
        if (audio.loop.sound != audio.next_loop) {
            audio.loop_gain -= FADE_STEP;
            if (audio.loop_gain <= 0.0f) {
                audio.loop_gain = 0.0f;
                start_voice(&audio.loop, audio.next_loop);
            }
        } else if (audio.loop_gain < 1.0f) {
            audio.loop_gain = fminf(1.0f, audio.loop_gain + FADE_STEP);
        }

        float mix = voice_sample(&audio.chomp) + audio.loop_gain * voice_sample(&audio.loop);
        for (int v = 0; v < NUM_FX_VOICES; v++) {
            mix += voice_sample(&audio.fx[v]);
        }
        out[i] = audio.master_gain * mix;
    }
}

static void init_audio(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("No audio, playing silently: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    // No allowed changes: SDL converts to whatever the hardware wants
    audio.device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio.device == 0) {
        printf("No audio device, playing silently: %s\n", SDL_GetError());
        return;
    }
    audio.master_gain = 1.0f;
    SDL_PauseAudioDevice(audio.device, 0);
}

static void close_audio(void) {
    if (audio.device != 0) {
        SDL_CloseAudioDevice(audio.device);
        audio.device = 0;
    }
}

static void play_sound(Sound sound) {
    if (audio.device == 0) {
        return;
    }
    SDL_LockAudioDevice(audio.device);
    if (sound == SOUND_WAKA) {
        // Pellets come about as fast as a half plays, so eating keeps the waka going
        if (audio.chomp.sound == SOUND_NONE) {
            start_voice(&audio.chomp, SOUND_WAKA);
            audio.chomp.half = audio.next_waka_half;
            audio.next_waka_half ^= 1;
        }
    } else {
        if (sound == SOUND_INTRO || sound == SOUND_DEATH) { // these play alone
            audio.chomp.sound = SOUND_NONE;
            for (int v = 0; v < NUM_FX_VOICES; v++) {
                audio.fx[v].sound = SOUND_NONE;
            }
        }
        // A free voice, or else the one that has played longest
        Voice *voice = &audio.fx[0];
        for (int v = 0; v < NUM_FX_VOICES; v++) {
            if (audio.fx[v].sound == SOUND_NONE) {
                voice = &audio.fx[v];
                break;
            }
            if (audio.fx[v].pos > voice->pos) {
                voice = &audio.fx[v];
            }
        }
        start_voice(voice, sound);
    }
    SDL_UnlockAudioDevice(audio.device);
}

// Picks the background loop; the audio thread fades from the old one to the new one.
static void set_loop(Sound sound, float siren_pitch) {
    if (audio.device == 0 || (audio.next_loop == sound && audio.siren_pitch == siren_pitch)) {
        return;
    }
    SDL_LockAudioDevice(audio.device);
    audio.next_loop = sound;
    audio.siren_pitch = siren_pitch;
    SDL_UnlockAudioDevice(audio.device);
}

static void set_audio_flags(bool muted, bool paused) {
    if (audio.device == 0) {
        audio.muted = muted;
        return;
    }
    SDL_LockAudioDevice(audio.device);
    audio.muted = muted;
    audio.paused = paused;
    SDL_UnlockAudioDevice(audio.device);
}

// ---------------------------------------------------------------------------
// Maze helpers
// ---------------------------------------------------------------------------

static Dir opposite(Dir d) {
    return d == DIR_NONE ? DIR_NONE : (Dir)((d + 2) % 4);
}

static int floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static int tile_col(int x) {
    return floor_div(x, TILE);
}

static int tile_row(int y) {
    return floor_div(y, TILE);
}

static bool at_center(int x, int y) {
    return ((x - TILE / 2) % TILE + TILE) % TILE == 0 && ((y - TILE / 2) % TILE + TILE) % TILE == 0;
}

// Walls as seen by Pac-Man and by ghosts finding their way. Outside the maze is a wall,
// except along the tunnel row, which wraps around.
static bool is_wall(int col, int row) {
    if (row < 0 || row >= MAZE_ROWS) {
        return true;
    }
    if (col < 0 || col >= MAZE_COLS) {
        return row != TUNNEL_ROW;
    }
    char c = game.maze[row][col];
    return c == '#' || c == '-';
}

// Keeps x inside the virtual columns -1..28; the tunnel connects them.
static void wrap_x(int *x) {
    const int span = (MAZE_COLS + 2) * TILE;
    if (*x < -TILE) {
        *x += span;
    } else if (*x >= MAZE_COLS * TILE + TILE) {
        *x -= span;
    }
}

static int take_steps(float *acc, float speed) {
    *acc += speed;
    int steps = (int)*acc;
    *acc -= (float)steps;
    return steps;
}

static int speed_tier(void) {
    return game.level == 1 ? 0 : (game.level <= 4 ? 1 : 2);
}

// ---------------------------------------------------------------------------
// Game setup
// ---------------------------------------------------------------------------

static void reset_maze(void) {
    game.dots_left = 0;
    game.dots_eaten_level = 0;
    for (int row = 0; row < MAZE_ROWS; row++) {
        memcpy(game.maze[row], MAZE_TEMPLATE[row], MAZE_COLS + 1);
        for (int col = 0; col < MAZE_COLS; col++) {
            if (game.maze[row][col] == '.' || game.maze[row][col] == 'o') {
                game.dots_left++;
            }
        }
    }
}

static void reset_actors(void) {
    Pacman *p = &game.pac;
    p->x = 14 * TILE; // between columns 13 and 14, like the arcade
    p->y = 23 * TILE + TILE / 2;
    p->dir = DIR_LEFT;
    p->next_dir = DIR_NONE;
    p->speed_acc = 0.0f;
    p->moving = false;

    static const struct {
        SDL_Color color;
        int home_x;
        int scatter_col, scatter_row;
        Dir dir;
    } defs[NUM_GHOSTS] = {
        { { 255, 0, 0, 255 }, DOOR_X, 25, -3, DIR_LEFT },              // Blinky
        { { 255, 184, 255, 255 }, DOOR_X, 2, -3, DIR_DOWN },           // Pinky
        { { 0, 255, 255, 255 }, DOOR_X - 2 * TILE, 27, 31, DIR_UP },   // Inky
        { { 255, 184, 82, 255 }, DOOR_X + 2 * TILE, 0, 31, DIR_UP },   // Clyde
    };

    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        g->id = i;
        g->color = defs[i].color;
        g->home_x = defs[i].home_x;
        g->scatter_col = defs[i].scatter_col;
        g->scatter_row = defs[i].scatter_row;
        g->dir = defs[i].dir;
        g->frightened = false;
        g->speed_acc = 0.0f;
        g->x = defs[i].home_x;
        g->y = HOUSE_Y;
        g->state = GHOST_IN_HOUSE;
    }
    // Blinky starts outside, right above the door
    game.ghosts[0].y = EXIT_Y;
    game.ghosts[0].state = GHOST_ACTIVE;

    game.mode_index = 0;
    game.mode_timer = (game.level >= 5 ? 5 : 7) * FPS;
    game.chase = false;
    game.fright_timer = 0;
    game.freeze_timer = 0;
    game.fruit_timer = 0;
    game.popup_timer = 0;
    game.dots_eaten_life = 0;
    game.no_dot_timer = 0;
    game.mouth_angle = 30.0f;
    game.mouth_opening = false;
}

static void enter_ready(int frames) {
    game.state = STATE_READY;
    game.state_timer = frames;
}

static void new_game(void) {
    game.score = 0;
    game.lives = START_LIVES;
    game.level = 1;
    game.extra_life_awarded = false;
    game.paused = false;
    set_audio_flags(audio.muted, false);
    reset_maze();
    reset_actors();
    enter_ready(4 * FPS);
    play_sound(SOUND_INTRO); // only a new game gets the jingle, like the arcade
}

static void next_level(void) {
    game.level++;
    reset_maze();
    reset_actors();
    enter_ready(2 * FPS);
}

static void add_score(int points) {
    game.score += points;
    if (!game.extra_life_awarded && game.score >= EXTRA_LIFE_SCORE) {
        game.extra_life_awarded = true;
        game.lives++;
        play_sound(SOUND_EXTRA_LIFE);
    }
    if (game.score > game.high_score) {
        game.high_score = game.score;
    }
}

static void load_high_score(void) {
    char *pref = SDL_GetPrefPath("gubatron", "pacman-c");
    if (pref == NULL) {
        return;
    }
    snprintf(high_score_path, sizeof(high_score_path), "%shighscore.txt", pref);
    SDL_free(pref);
    FILE *f = fopen(high_score_path, "r");
    if (f != NULL) {
        if (fscanf(f, "%d", &game.high_score) != 1) {
            game.high_score = 0;
        }
        fclose(f);
    }
}

static void save_high_score(void) {
    if (high_score_path[0] == '\0') {
        return;
    }
    FILE *f = fopen(high_score_path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", game.high_score);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------
// Pac-Man
// ---------------------------------------------------------------------------

static void start_fright(void) {
    static const int FRIGHT_SECONDS[] = { 6, 5, 4, 3, 2, 5, 2, 2, 1, 5, 2, 1, 1, 3, 1, 1, 0, 1 };
    const int levels = (int)(sizeof(FRIGHT_SECONDS) / sizeof(FRIGHT_SECONDS[0]));
    int seconds = game.level > levels ? 0 : FRIGHT_SECONDS[game.level - 1];

    game.ghosts_eaten_combo = 0;
    game.fright_timer = seconds * FPS;
    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        if (g->state == GHOST_ACTIVE) {
            g->dir = opposite(g->dir);
        }
        if (seconds > 0 && g->state != GHOST_EYES && g->state != GHOST_ENTERING) {
            g->frightened = true;
        }
    }
}

static void pellet_eaten(void) {
    game.dots_left--;
    game.dots_eaten_level++;
    game.dots_eaten_life++;
    game.no_dot_timer = 0;
    if (game.dots_eaten_level == 70 || game.dots_eaten_level == 170) {
        game.fruit_timer = FPS * 19 / 2; // fruit stays for 9.5 seconds
    }
}

static void eat_at_pacman(void) {
    int col = tile_col(game.pac.x);
    int row = tile_row(game.pac.y);
    if (col < 0 || col >= MAZE_COLS) {
        return;
    }
    char *c = &game.maze[row][col];
    if (*c == '.') {
        *c = ' ';
        add_score(10);
        pellet_eaten();
        play_sound(SOUND_WAKA);
    } else if (*c == 'o') {
        *c = ' ';
        add_score(50);
        pellet_eaten();
        play_sound(SOUND_WAKA);
        start_fright();
    }
}

// Moves Pac-Man one pixel. Turns happen at tile centers; reversing works anywhere.
static bool step_pacman(void) {
    Pacman *p = &game.pac;
    if (p->next_dir != DIR_NONE && p->next_dir == opposite(p->dir)) {
        p->dir = p->next_dir;
    }
    if (at_center(p->x, p->y)) {
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

static void update_pacman(void) {
    static const float NORMAL[] = { 0.80f, 0.90f, 1.00f };
    static const float FRIGHT[] = { 0.90f, 0.95f, 1.00f };
    float pct = game.fright_timer > 0 ? FRIGHT[speed_tier()] : NORMAL[speed_tier()];
    int steps = take_steps(&game.pac.speed_acc, BASE_SPEED * pct);
    for (int i = 0; i < steps; i++) {
        if (!step_pacman()) {
            break;
        }
        eat_at_pacman();
    }
}

void update_pacman_mouth_angle(bool *mouth_opening, float *mouth_angle) {
    if (*mouth_opening) {
        *mouth_angle += MOUTH_SPEED;
        if (*mouth_angle >= 70.0f) {
            *mouth_angle = 70.0f;
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

// ---------------------------------------------------------------------------
// Ghosts
// ---------------------------------------------------------------------------

// Each ghost's personality is the tile it heads for.
static void ghost_target(const Ghost *g, int *tx, int *ty) {
    if (g->state == GHOST_EYES) {
        *tx = EYES_TARGET_COL;
        *ty = EYES_TARGET_ROW;
        return;
    }
    if (!game.chase) {
        *tx = g->scatter_col;
        *ty = g->scatter_row;
        return;
    }

    int pc = tile_col(game.pac.x);
    int pr = tile_row(game.pac.y);
    Dir pd = game.pac.dir;
    switch (g->id) {
    case 0: // Blinky chases Pac-Man directly
        *tx = pc;
        *ty = pr;
        break;
    case 1: // Pinky aims four tiles ahead of Pac-Man
        *tx = pc + 4 * DX[pd];
        *ty = pr + 4 * DY[pd];
        break;
    case 2: { // Inky doubles the vector from Blinky to two tiles ahead of Pac-Man
        const Ghost *blinky = &game.ghosts[0];
        int ax = pc + 2 * DX[pd];
        int ay = pr + 2 * DY[pd];
        *tx = 2 * ax - tile_col(blinky->x);
        *ty = 2 * ay - tile_row(blinky->y);
        break;
    }
    default: { // Clyde chases until he gets within 8 tiles, then runs to his corner
        int dx = tile_col(g->x) - pc;
        int dy = tile_row(g->y) - pr;
        if (dx * dx + dy * dy > 64) {
            *tx = pc;
            *ty = pr;
        } else {
            *tx = g->scatter_col;
            *ty = g->scatter_row;
        }
        break;
    }
    }
}

// At a tile center ghosts never reverse; they take the open turn closest to their target,
// or a random one while frightened.
static Dir choose_direction(const Ghost *g, int col, int row) {
    Dir options[4];
    int count = 0;
    for (int d = 0; d < 4; d++) {
        if (d == opposite(g->dir) || is_wall(col + DX[d], row + DY[d])) {
            continue;
        }
        options[count++] = (Dir)d;
    }
    if (count == 0) {
        return opposite(g->dir);
    }
    if (g->frightened && g->state == GHOST_ACTIVE) {
        return options[rand() % count];
    }

    int tx, ty;
    ghost_target(g, &tx, &ty);
    Dir best = options[0];
    long best_dist = LONG_MAX;
    for (int i = 0; i < count; i++) {
        long dx = col + DX[options[i]] - tx;
        long dy = row + DY[options[i]] - ty;
        long dist = dx * dx + dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best = options[i];
        }
    }
    return best;
}

static void move_toward(int *value, int target) {
    if (*value < target) {
        (*value)++;
    } else if (*value > target) {
        (*value)--;
    }
}

// Moves a ghost one pixel.
static void step_ghost(Ghost *g) {
    switch (g->state) {
    case GHOST_IN_HOUSE: // bob up and down until released
        g->y += DY[g->dir];
        if (g->y <= HOUSE_Y - 6) {
            g->dir = DIR_DOWN;
        } else if (g->y >= HOUSE_Y + 6) {
            g->dir = DIR_UP;
        }
        return;
    case GHOST_LEAVING:
        if (g->x != DOOR_X) {
            if (g->y != HOUSE_Y) {
                move_toward(&g->y, HOUSE_Y);
            } else {
                move_toward(&g->x, DOOR_X);
            }
        } else if (g->y > EXIT_Y) {
            g->y--;
            g->dir = DIR_UP;
        } else {
            g->state = GHOST_ACTIVE;
            g->dir = DIR_LEFT;
        }
        return;
    case GHOST_ENTERING: // eyes go back in through the door and the ghost revives
        if (g->y < HOUSE_Y && g->x != DOOR_X) {
            move_toward(&g->x, DOOR_X);
        } else if (g->y < HOUSE_Y) {
            g->y++;
            g->dir = DIR_DOWN;
        } else if (g->x != g->home_x) {
            move_toward(&g->x, g->home_x);
        } else {
            g->frightened = false;
            g->state = GHOST_LEAVING;
        }
        return;
    case GHOST_ACTIVE:
    case GHOST_EYES:
        break;
    }

    if (at_center(g->x, g->y)) {
        int col = tile_col(g->x);
        int row = tile_row(g->y);
        if (g->state == GHOST_EYES && col == EYES_TARGET_COL && row == EYES_TARGET_ROW) {
            g->state = GHOST_ENTERING;
            return;
        }
        g->dir = choose_direction(g, col, row);
    }
    g->x += DX[g->dir];
    g->y += DY[g->dir];
    wrap_x(&g->x);
}

static float ghost_speed(const Ghost *g) {
    static const float NORMAL[] = { 0.75f, 0.85f, 0.95f };
    static const float FRIGHT[] = { 0.50f, 0.55f, 0.60f };
    static const float TUNNEL[] = { 0.40f, 0.45f, 0.50f };
    int tier = speed_tier();
    switch (g->state) {
    case GHOST_EYES:
    case GHOST_ENTERING:
        return BASE_SPEED * 1.6f;
    case GHOST_IN_HOUSE:
    case GHOST_LEAVING:
        return BASE_SPEED * 0.4f;
    case GHOST_ACTIVE:
        break;
    }
    int col = tile_col(g->x);
    if (tile_row(g->y) == TUNNEL_ROW && (col <= 5 || col >= MAZE_COLS - 6)) {
        return BASE_SPEED * TUNNEL[tier];
    }
    return BASE_SPEED * (g->frightened ? FRIGHT[tier] : NORMAL[tier]);
}

// Pinky leaves right away; Inky and Clyde wait for Pac-Man to eat some pellets,
// or for him to stop eating for four seconds.
static void release_ghosts(void) {
    static const int RELEASE_DOTS[3][NUM_GHOSTS] = {
        { 0, 0, 30, 60 },
        { 0, 0, 0, 50 },
        { 0, 0, 0, 0 },
    };
    int tier = game.level == 1 ? 0 : (game.level == 2 ? 1 : 2);
    for (int i = 1; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        if (g->state != GHOST_IN_HOUSE) {
            continue;
        }
        if (game.dots_eaten_life >= RELEASE_DOTS[tier][i] || game.no_dot_timer >= 4 * FPS) {
            g->state = GHOST_LEAVING;
            game.no_dot_timer = 0;
        }
        break; // one at a time, in order
    }
}

static void update_modes(void) {
    static const int MODE_SECONDS[] = { 7, 20, 7, 20, 5, 20, 5, -1 };
    const int last = (int)(sizeof(MODE_SECONDS) / sizeof(MODE_SECONDS[0])) - 1;

    if (game.fright_timer > 0) {
        if (--game.fright_timer == 0) {
            for (int i = 0; i < NUM_GHOSTS; i++) {
                game.ghosts[i].frightened = false;
            }
        }
        return; // the scatter/chase clock stops while ghosts are frightened
    }
    if (game.mode_timer > 0 && --game.mode_timer == 0) {
        game.mode_index++;
        game.chase = game.mode_index % 2 == 1;
        int seconds = MODE_SECONDS[game.mode_index < last ? game.mode_index : last];
        game.mode_timer = seconds < 0 ? -1 : seconds * FPS;
        for (int i = 0; i < NUM_GHOSTS; i++) {
            if (game.ghosts[i].state == GHOST_ACTIVE) {
                game.ghosts[i].dir = opposite(game.ghosts[i].dir);
            }
        }
    }
}

static void check_collisions(void) {
    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        if (g->state != GHOST_ACTIVE || abs(g->x - game.pac.x) + abs(g->y - game.pac.y) >= TILE * 3 / 5) {
            continue;
        }
        if (g->frightened) {
            game.ghosts_eaten_combo++;
            int points = 200 << (game.ghosts_eaten_combo - 1);
            add_score(points);
            g->frightened = false;
            g->state = GHOST_EYES;
            game.popup_points = points;
            game.popup_x = g->x;
            game.popup_y = g->y;
            game.popup_timer = FPS;
            game.freeze_timer = FPS / 2;
            play_sound(SOUND_GHOST_EATEN);
        } else {
            game.state = STATE_DYING;
            game.state_timer = 2 * FPS;
            return;
        }
    }

    if (game.fruit_timer > 0 && abs(game.pac.x - FRUIT_X) < TILE / 2 && abs(game.pac.y - FRUIT_Y) < TILE / 2) {
        static const int FRUIT_POINTS[] = { 100, 300, 500, 500, 700, 700, 1000, 1000, 2000, 2000, 3000, 3000, 5000 };
        const int count = (int)(sizeof(FRUIT_POINTS) / sizeof(FRUIT_POINTS[0]));
        int points = FRUIT_POINTS[game.level <= count ? game.level - 1 : count - 1];
        add_score(points);
        game.fruit_timer = 0;
        game.popup_points = points;
        game.popup_x = FRUIT_X;
        game.popup_y = FRUIT_Y;
        game.popup_timer = FPS * 3 / 2;
        play_sound(SOUND_FRUIT);
    }
}

static void after_death(void) {
    game.lives--;
    if (game.lives <= 0) {
        game.state = STATE_GAME_OVER;
        save_high_score();
        return;
    }
    reset_actors();
    enter_ready(2 * FPS);
}

// The siren plays while Pac-Man is out there, the frightened sound while a ghost is blue,
// and the eyes sound while eaten ghosts head home, which beats both.
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
    // The siren steps up four times as the maze empties
    int total = game.dots_left + game.dots_eaten_level;
    float pitch = total > 0 ? floorf(4.0f * (float)game.dots_eaten_level / (float)total) / 4.0f : 0.0f;
    set_loop(loop, pitch);
}

static void update_game(void) {
    game.frame++;
    if (game.popup_timer > 0) {
        game.popup_timer--;
    }

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
            play_sound(SOUND_DEATH); // as the dying animation starts
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
    if (game.freeze_timer > 0) {
        game.freeze_timer--;
        return;
    }

    update_modes();

    update_pacman();
    if (game.pac.moving) {
        update_pacman_mouth_angle(&game.mouth_opening, &game.mouth_angle);
    }
    if (game.dots_left == 0) {
        game.state = STATE_LEVEL_DONE;
        game.state_timer = 5 * FPS / 2;
        return;
    }
    check_collisions();
    if (game.state != STATE_PLAYING) {
        return;
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &game.ghosts[i];
        int steps = take_steps(&g->speed_acc, ghost_speed(g));
        for (int s = 0; s < steps; s++) {
            step_ghost(g);
        }
    }
    game.no_dot_timer++;
    release_ghosts();
    check_collisions();

    if (game.fruit_timer > 0) {
        game.fruit_timer--;
    }
}

static void update(void) {
    update_game();
    update_loop_sound();
}

static void handle_keydown(SDL_Scancode key, bool *quit) {
    switch (key) {
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W:
        game.pac.next_dir = DIR_UP;
        break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
        game.pac.next_dir = DIR_DOWN;
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
        game.pac.next_dir = DIR_LEFT;
        break;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
        game.pac.next_dir = DIR_RIGHT;
        break;
    case SDL_SCANCODE_P:
        if (game.state == STATE_PLAYING) {
            game.paused = !game.paused;
            set_audio_flags(audio.muted, game.paused);
        }
        break;
    case SDL_SCANCODE_M:
        set_audio_flags(!audio.muted, game.paused);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_SPACE:
        if (game.state == STATE_GAME_OVER) {
            new_game();
        }
        break;
    case SDL_SCANCODE_ESCAPE:
        *quit = true;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static void set_color(SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

static void fill_circle(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++) {
        int half = (int)sqrtf((float)(r * r - dy * dy));
        SDL_RenderDrawLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
    }
}

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

void draw_pacman(int cx, int cy, int radius, float mouth_angle, float facing) {
    static SDL_Point points[(2 * 16 + 1) * (2 * 16 + 1)];
    int count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            float angle = atan2f((float)dy, (float)dx) * (180.0f / PI_F);
            // Only the points outside the mouth's opening
            if (angle_diff(angle, facing) > mouth_angle / 2.0f) {
                points[count].x = cx + dx;
                points[count].y = cy + dy;
                count++;
            }
        }
    }
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Yellow color
    SDL_RenderDrawPoints(renderer, points, count);
}

static void draw_cherry(int cx, int cy) {
    SDL_SetRenderDrawColor(renderer, 222, 151, 81, 255); // stems
    SDL_RenderDrawLine(renderer, cx - 5, cy + 2, cx + 5, cy - 9);
    SDL_RenderDrawLine(renderer, cx + 5, cy + 1, cx + 5, cy - 9);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    fill_circle(cx - 5, cy + 4, 5);
    fill_circle(cx + 5, cy + 3, 5);
}

static bool wall_for_drawing(int col, int row) {
    if (col < 0 || col >= MAZE_COLS || row < 0 || row >= MAZE_ROWS) {
        return true;
    }
    return game.maze[row][col] == '#' || game.maze[row][col] == '-';
}

static void hline(int x1, int x2, int y) {
    SDL_Rect r = { x1 < x2 ? x1 : x2, y - 1, abs(x2 - x1) + 1, 2 };
    SDL_RenderFillRect(renderer, &r);
}

static void vline(int x, int y1, int y2) {
    SDL_Rect r = { x - 1, y1 < y2 ? y1 : y2, 2, abs(y2 - y1) + 1 };
    SDL_RenderFillRect(renderer, &r);
}

// Walls are drawn as outlines: a line wherever a wall tile faces an open tile.
static void draw_maze(SDL_Color color) {
    const int inset = TILE / 3;
    set_color(color);
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            int left = col * TILE;
            int top = HUD_HEIGHT + row * TILE;
            int right = left + TILE;
            int bottom = top + TILE;

            if (game.maze[row][col] == '-') {
                SDL_SetRenderDrawColor(renderer, 255, 184, 255, 255);
                SDL_Rect door = { left, top + TILE / 2 - 2, TILE, 4 };
                SDL_RenderFillRect(renderer, &door);
                set_color(color);
                continue;
            }
            if (game.maze[row][col] != '#') {
                continue;
            }

            bool open_up = !wall_for_drawing(col, row - 1);
            bool open_down = !wall_for_drawing(col, row + 1);
            bool open_left = !wall_for_drawing(col - 1, row);
            bool open_right = !wall_for_drawing(col + 1, row);

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

            // Inside corners: both neighbors are walls but the diagonal is open
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
    bool show_power = game.state != STATE_PLAYING || (game.frame / 10) % 2 == 0;
    SDL_SetRenderDrawColor(renderer, 255, 184, 151, 255);
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            int cx = col * TILE + TILE / 2;
            int cy = HUD_HEIGHT + row * TILE + TILE / 2;
            if (game.maze[row][col] == '.') {
                SDL_Rect dot = { cx - 2, cy - 2, 4, 4 };
                SDL_RenderFillRect(renderer, &dot);
            } else if (game.maze[row][col] == 'o' && show_power) {
                fill_circle(cx, cy, 7);
            }
        }
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
            flash = game.fright_timer < 2 * FPS && (game.frame / 10) % 2 == 0;
            body = flash ? (SDL_Color){ 255, 255, 255, 255 } : (SDL_Color){ 33, 33, 255, 255 };
        }
        set_color(body);
        // Dome, body and a skirt whose feet wiggle as it moves
        for (int dy = -r; dy <= 0; dy++) {
            int half = (int)sqrtf((float)(r * r - dy * dy));
            SDL_RenderDrawLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
        }
        SDL_Rect rect = { cx - r, cy, 2 * r + 1, r - 3 };
        SDL_RenderFillRect(renderer, &rect);
        int phase = (game.frame / 8) % 2;
        for (int x = -r; x <= r; x++) {
            int foot = ((x + r + phase * 3) / 4) % 2 == 0 ? 4 : 0;
            SDL_RenderDrawLine(renderer, cx + x, cy + r - 3, cx + x, cy + r - 3 + foot);
        }

        if (g->frightened) {
            SDL_Color face = flash ? (SDL_Color){ 255, 0, 0, 255 } : (SDL_Color){ 255, 184, 151, 255 };
            set_color(face);
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

    // Eyes look where the ghost is heading
    int ex = g->dir == DIR_NONE ? 0 : DX[g->dir] * 2;
    int ey = g->dir == DIR_NONE ? 0 : DY[g->dir] * 2;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    fill_circle(cx - 5 + ex / 2, cy - 3 + ey / 2, 4);
    fill_circle(cx + 5 + ex / 2, cy - 3 + ey / 2, 4);
    SDL_SetRenderDrawColor(renderer, 33, 33, 255, 255);
    fill_circle(cx - 5 + ex, cy - 3 + ey, 2);
    fill_circle(cx + 5 + ex, cy - 3 + ey, 2);
}

static void draw_hud(void) {
    const SDL_Color white = { 255, 255, 255, 255 };
    char text[64];

    draw_text("1UP", 3 * TILE, 2, white, false);
    snprintf(text, sizeof(text), "%d", game.score);
    draw_text(text, 3 * TILE, 24, white, false);
    draw_text("HIGH SCORE", WINDOW_WIDTH / 2, 2, white, true);
    snprintf(text, sizeof(text), "%d", game.high_score);
    draw_text(text, WINDOW_WIDTH / 2, 24, white, true);

    // Reserve lives at the bottom left, level at the bottom right
    int bottom = HUD_HEIGHT + MAZE_ROWS * TILE;
    for (int i = 0; i < game.lives - 1 && i < 6; i++) {
        draw_pacman(2 * TILE + i * 30, bottom + TILE, 9, 60.0f, 180.0f);
    }
    snprintf(text, sizeof(text), "LEVEL %d", game.level);
    int text_width = 0;
    TTF_SizeText(font, text, &text_width, NULL);
    int text_x = WINDOW_WIDTH - TILE - text_width;
    draw_text(text, text_x, bottom + 12, white, false);
    draw_cherry(text_x - TILE, bottom + TILE);
    if (audio.muted) {
        draw_text("MUTED (M)", WINDOW_WIDTH / 2, bottom + 12, (SDL_Color){ 128, 128, 128, 255 }, true);
    }
}

static void render(void) {
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Color wall_color = { 33, 33, 222, 255 };
    if (game.state == STATE_LEVEL_DONE && game.state_timer < 2 * FPS && (game.state_timer / 15) % 2 == 0) {
        wall_color = (SDL_Color){ 255, 255, 255, 255 };
    }
    draw_maze(wall_color);
    draw_pellets();

    if (game.fruit_timer > 0) {
        draw_cherry(FRUIT_X, HUD_HEIGHT + FRUIT_Y);
    }

    bool dying_animation = game.state == STATE_DYING && game.state_timer <= 3 * FPS / 2;
    bool show_ghosts = game.state != STATE_LEVEL_DONE && game.state != STATE_GAME_OVER && !dying_animation;
    if (show_ghosts) {
        for (int i = 0; i < NUM_GHOSTS; i++) {
            draw_ghost(&game.ghosts[i]);
        }
    }

    int pac_y = game.pac.y + HUD_HEIGHT;
    if (dying_animation) {
        // The mouth opens all the way around until Pac-Man disappears
        float progress = 1.0f - (float)game.state_timer / (3 * FPS / 2);
        draw_pacman(game.pac.x, pac_y, PLAYER_RADIUS, progress * 380.0f, -90.0f);
    } else if (game.state == STATE_READY || game.state == STATE_LEVEL_DONE) {
        draw_pacman(game.pac.x, pac_y, PLAYER_RADIUS, 0.0f, 0.0f);
    } else if (game.state != STATE_GAME_OVER) {
        draw_pacman(game.pac.x, pac_y, PLAYER_RADIUS, game.mouth_angle, FACING_DEGREES[game.pac.dir]);
    }

    if (game.popup_timer > 0) {
        char text[16];
        snprintf(text, sizeof(text), "%d", game.popup_points);
        draw_text(text, game.popup_x, HUD_HEIGHT + game.popup_y - 12, (SDL_Color){ 0, 255, 255, 255 }, true);
    }

    int message_y = HUD_HEIGHT + 17 * TILE;
    if (game.state == STATE_READY) {
        draw_text("READY!", WINDOW_WIDTH / 2, message_y, (SDL_Color){ 255, 255, 0, 255 }, true);
    } else if (game.state == STATE_GAME_OVER) {
        draw_text("GAME  OVER", WINDOW_WIDTH / 2, message_y, (SDL_Color){ 255, 0, 0, 255 }, true);
        draw_text("PRESS ENTER TO PLAY AGAIN", WINDOW_WIDTH / 2, HUD_HEIGHT + 20 * TILE, (SDL_Color){ 255, 255, 255, 255 }, true);
    } else if (game.paused) {
        draw_text("PAUSED", WINDOW_WIDTH / 2, message_y, (SDL_Color){ 255, 255, 0, 255 }, true);
    }

    draw_hud();

    // Update screen
    SDL_RenderPresent(renderer);
}

float angle_diff(float a, float b) {
    float diff = a - b;
    while (diff < -180.0f) diff += 360.0f;
    while (diff > 180.0f) diff -= 360.0f;
    return fabsf(diff);
}

#ifdef PACMAN_DEBUG
// Headless soak test: a random bot plays for the given number of frames while invariants
// are checked, every 5000th frame clears the board to exercise level transitions, and the
// last frame can be saved as a BMP. Build with -DPACMAN_DEBUG and run with
// SDL_VIDEODRIVER=dummy PACMAN_FRAMES=n [PACMAN_SHOT=file.bmp].
static int run_debug(int frames, const char *screenshot_path) {
    int games = 1, max_level = 1, best_score = 0;
    new_game();
    for (int f = 0; f < frames; f++) {
        Pacman *p = &game.pac;
        if (game.state == STATE_PLAYING && at_center(p->x, p->y)) {
            int col = tile_col(p->x), row = tile_row(p->y);
            Dir options[4];
            int count = 0;
            for (int d = 0; d < 4; d++) {
                if (d != opposite(p->dir) && !is_wall(col + DX[d], row + DY[d])) {
                    options[count++] = (Dir)d;
                }
            }
            p->next_dir = count > 0 ? options[rand() % count] : opposite(p->dir);
        }
        if (game.state == STATE_PLAYING && f % 5000 == 4999) {
            for (int row = 0; row < MAZE_ROWS; row++) {
                for (int col = 0; col < MAZE_COLS; col++) {
                    if (game.maze[row][col] == '.' || game.maze[row][col] == 'o') {
                        game.maze[row][col] = ' ';
                    }
                }
            }
            game.dots_left = 0;
        }

        update();

        if (at_center(p->x, p->y) && is_wall(tile_col(p->x), tile_row(p->y))) {
            printf("FAIL frame %d: Pac-Man inside a wall at (%d, %d)\n", f, p->x, p->y);
            return 1;
        }
        for (int i = 0; i < NUM_GHOSTS; i++) {
            const Ghost *g = &game.ghosts[i];
            if ((g->state == GHOST_ACTIVE || g->state == GHOST_EYES) && at_center(g->x, g->y) &&
                is_wall(tile_col(g->x), tile_row(g->y))) {
                printf("FAIL frame %d: ghost %d inside a wall at (%d, %d)\n", f, i, g->x, g->y);
                return 1;
            }
            if (g->x < -2 * TILE || g->x > (MAZE_COLS + 2) * TILE || g->y < 0 || g->y > MAZE_ROWS * TILE) {
                printf("FAIL frame %d: ghost %d left the maze at (%d, %d)\n", f, i, g->x, g->y);
                return 1;
            }
        }
        if (game.dots_left < 0) {
            printf("FAIL frame %d: dots_left went negative\n", f);
            return 1;
        }
        if (game.level > max_level) max_level = game.level;
        if (game.score > best_score) best_score = game.score;
        if (game.state == STATE_GAME_OVER && f < frames - 1) {
            games++;
            new_game();
        }
    }
    render();
    if (screenshot_path != NULL) {
        SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_WIDTH, WINDOW_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
        if (shot != NULL && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch) == 0) {
            SDL_SaveBMP(shot, screenshot_path);
        }
        SDL_FreeSurface(shot);
    }
    printf("OK %d frames, %d games, max level %d, best score %d, final state %d, dots left %d\n", frames, games,
           max_level, best_score, game.state, game.dots_left);
    return 0;
}
#endif
