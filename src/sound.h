// sound.h
// Sound effects and music, synthesized while they play: there are no sound files.
//
// The rest of the game only says *what* should be heard (play the waka, switch the
// background loop to the siren); sound.c works out the samples. sound.c knows nothing
// about the game, which makes it easy to read on its own.
//
// If the computer has no audio device, every function here does nothing (except
// remembering the mute setting) and the game plays silently.
#ifndef PACMAN_SOUND_H
#define PACMAN_SOUND_H

#include <stdbool.h>

typedef enum {
    SOUND_NONE,
    // One-shot sounds, which stop on their own
    SOUND_INTRO,       // the jingle while READY! shows at the start of a game
    SOUND_WAKA,        // one half of "waka-waka", played per pellet
    SOUND_GHOST_EATEN,
    SOUND_FRUIT,
    SOUND_EXTRA_LIFE,
    SOUND_DEATH,
    // Background loops, which play until replaced. Keep these last: sound.c tells loops
    // from one-shots with `sound >= SOUND_SIREN`.
    SOUND_SIREN,  // the normal background, rising in pitch as the maze empties
    SOUND_FRIGHT, // while ghosts are blue
    SOUND_EYES,   // while eaten ghosts race home
} Sound;

// Opens the audio device. Prints a message and carries on silently if there is none.
void sound_init(void);
void sound_quit(void);

// Starts a one-shot sound. The intro and the death sound silence the other effects.
void sound_play(Sound sound);

// Chooses the background loop (or SOUND_NONE for silence) and how high the siren is,
// from 0 with a full maze to 1 near the last pellet. Call it every frame; it only does
// work when something changed. The old loop fades out before the new one starts.
void sound_set_loop(Sound loop, float siren_pitch);

// Mute fades everything out until unmuted. Pause also freezes every sound where it is,
// so it continues from the same spot on unpause.
void sound_set_muted(bool muted);
bool sound_is_muted(void);
void sound_set_paused(bool paused);

#endif
