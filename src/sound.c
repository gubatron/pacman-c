// sound.c
// A tiny software synthesizer, a bit like the arcade's wavetable sound chip.
//
// How digital sound works, in one paragraph: a speaker is driven by a stream of numbers
// called samples, AUDIO_RATE of them per second. Each sample says how far the speaker
// cone should be pushed, from -1 to 1. A tone of 440 Hz is a wave that goes up and down
// 440 times per second; a louder tone swings further from 0. To play several sounds at
// once you just add their samples together ("mixing").
//
// How SDL plays it: we open an audio device and give SDL a callback function. SDL calls
// it on its own audio thread, many times per second, each time handing us a buffer to
// fill with the next few hundred samples. If we are late, the sound stutters, so the
// callback must be quick and must never wait on anything slow.
//
// Threads: the game runs on the main thread and the callback on the audio thread, and
// both touch the `audio` struct below. If the game changed a voice while the callback
// was halfway through reading it, the callback could see half old and half new data.
// So the game only changes `audio` between SDL_LockAudioDevice and SDL_UnlockAudioDevice,
// which makes SDL hold the callback until we are done.
#include "sound.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>

#define AUDIO_RATE    44100 // samples per second, the CD standard
#define NUM_FX_VOICES 3     // how many one-shot effects can play at the same time

// Loops, mute and pause fade in and out over 10 ms instead of switching instantly. A
// sudden jump in the wave is heard as a click. This is how much the volume changes per
// sample to cover the whole range in 10 ms.
#define FADE_STEP (1.0f / (0.01f * AUDIO_RATE))

// The intro jingle is counted in sixteenth notes of this many seconds. 64 of them make
// 3.9 seconds, which fits in the 4 seconds that READY! shows at the start of a game.
#define INTRO_UNIT 0.061

// A voice plays one sound. It remembers how far into the sound it is and where its
// waves are, so it can carry on exactly where it stopped in the next callback.
typedef struct {
    Sound sound; // SOUND_NONE when the voice is free
    int pos;     // samples played so far
    int length;  // total samples, or 0 for a loop that never ends
    int half;    // for the waka: 0 plays "wa", 1 plays "ka"
    double phase, bass_phase; // progress through the current wave, in cycles (see tone())
} Voice;

// Everything the audio thread reads. Change it only while the device is locked (see the
// top of this file), except `device` and `muted`, which only the main thread uses
// outside the callback.
typedef struct {
    SDL_AudioDeviceID device; // 0 when there is no audio
    Voice chomp;              // the waka gets a voice of its own so it is never cut off
    Voice fx[NUM_FX_VOICES];  // the other one-shot sounds
    Voice loop;               // siren, frightened or eyes: one at a time
    Sound next_loop;          // the loop to switch to once the current one has faded out
    float loop_gain;          // volume of the loop voice, for fading between loops
    float siren_pitch;        // 0 with a full maze, 1 near the last pellet
    float master_gain;        // overall volume, for fading mute and pause
    bool muted, paused;
    int next_waka_half;       // the waka alternates "wa", "ka", "wa", ...
} Audio;

static Audio audio;

// ---------------------------------------------------------------------------
// The music
// ---------------------------------------------------------------------------

// The intro jingle as a list of notes: MIDI note numbers (69 is the A at 440 Hz, and
// each step is a semitone) and how many sixteenths each note lasts.
//
// C lesson: an array of an anonymous struct, declared and filled in one go.
static const struct {
    int note, units;
} INTRO_MELODY[] = {
    { 71, 2 }, { 83, 2 }, { 78, 2 }, { 75, 2 }, { 83, 1 }, { 78, 3 }, { 75, 4 },
    { 72, 2 }, { 84, 2 }, { 79, 2 }, { 76, 2 }, { 84, 1 }, { 79, 3 }, { 76, 4 },
    { 71, 2 }, { 83, 2 }, { 78, 2 }, { 75, 2 }, { 83, 1 }, { 78, 3 }, { 75, 4 },
    { 75, 1 }, { 76, 1 }, { 77, 2 }, { 77, 1 }, { 78, 1 }, { 79, 2 }, { 79, 1 }, { 80, 1 }, { 81, 2 }, { 83, 4 },
};

// The bass line under the melody: one note per eighth (two sixteenths).
static const int INTRO_BASS[32] = {
    47, 59, 47, 59, 47, 59, 47, 59, 48, 60, 48, 60, 48, 60, 48, 60,
    47, 59, 47, 59, 47, 59, 47, 59, 54, 54, 56, 56, 58, 58, 59, 59,
};

// ---------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------

// The frequency of a MIDI note: every 12 semitones up doubles the frequency.
static double note_freq(int note) {
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

// A triangle wave, the soft buzzy tone of the arcade chip. `phase` counts cycles: the
// whole part says how many cycles have gone by, the fraction where we are in the
// current one. The result climbs from -1 to 1 over the first half and falls back.
static float triangle(double phase) {
    double f = phase - floor(phase);
    return (float)(f < 0.5 ? 4.0 * f - 1.0 : 3.0 - 4.0 * f);
}

// A volume shape that rises from 0 to 1 in the first 4 ms and falls back to 0 in the
// last 12 ms of a sound `length` seconds long. Without it, sounds would start and stop
// with a click. `t` is the time into the sound, in seconds.
static float envelope(double t, double length) {
    double e = fmin(t / 0.004, (length - t) / 0.012);
    return e >= 1.0 ? 1.0f : (e <= 0.0 ? 0.0f : (float)e);
}

// The next sample of a triangle wave at `freq` Hz.
//
// Why keep a running phase instead of computing triangle(freq * t)? Because most of our
// sounds slide their frequency up or down. With freq * t, changing freq would make the
// wave jump to a different point, which sounds like a click. Advancing the phase a
// little each sample (freq / AUDIO_RATE cycles) keeps the wave smooth whatever freq does.
static float tone(Voice *v, double freq) {
    v->phase += freq / AUDIO_RATE;
    return triangle(v->phase);
}

// ---------------------------------------------------------------------------
// The sounds
// ---------------------------------------------------------------------------

// One sample of a sound, `t` seconds after it started. Each case is a small recipe; the
// constants were tuned by ear to resemble the arcade.
static float sound_sample(Voice *v, double t) {
    switch (v->sound) {
    case SOUND_INTRO: {
        // Find the melody note that is playing at time t by walking the note list
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
        // Add the bass note for this eighth, with its own phase so the two waves don't
        // disturb each other
        int eighth = (int)(units / 2.0);
        if (eighth < 32) {
            v->bass_phase += note_freq(INTRO_BASS[eighth]) / AUDIO_RATE;
            out += 0.16f * triangle(v->bass_phase) * envelope(t - eighth * 2 * INTRO_UNIT, 2 * INTRO_UNIT);
        }
        return out;
    }
    case SOUND_WAKA: { // "wa" sweeps down from 1200 Hz to 500 Hz, "ka" sweeps back up
        double x = t / 0.11;
        double freq = v->half == 0 ? 1200.0 - 700.0 * x : 500.0 + 700.0 * x;
        return 0.22f * tone(v, freq);
    }
    case SOUND_GHOST_EATEN: // a fast rising sweep while the game freezes
        return 0.18f * tone(v, 150.0 * pow(12.0, t / 0.5));
    case SOUND_FRUIT: // a warble up and down
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
    case SOUND_SIREN: { // a sine wave that rises and falls, higher the fewer pellets are left
        double base = 380.0 + 260.0 * audio.siren_pitch;
        v->phase += base * (1.0 + 0.35 * (triangle(t / 0.42) + 1.0) / 2.0) / AUDIO_RATE;
        return 0.1f * sinf((float)(2.0 * M_PI * v->phase));
    }
    case SOUND_FRIGHT: // quick rising sweeps
        return 0.11f * tone(v, 250.0 * pow(4.0, fmod(t, 0.135) / 0.135));
    case SOUND_EYES: // quick falling sweeps
        return 0.09f * tone(v, 1500.0 * pow(0.4, fmod(t, 0.1) / 0.1));
    case SOUND_NONE:
        break;
    }
    return 0.0f;
}

// The next sample of a voice, which then moves one sample ahead. A one-shot voice frees
// itself when its sound ends.
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
    // How long each one-shot sound lasts, in seconds.
    //
    // C lesson: designated initializers. "[SOUND_WAKA] = 0.11" puts 0.11 at index
    // SOUND_WAKA, so the table stays right even if the enum is reordered.
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

// ---------------------------------------------------------------------------
// The mixer, on the audio thread
// ---------------------------------------------------------------------------

// SDL calls this whenever it needs more sound: fill `stream` with `len` bytes of samples.
// We asked for 32-bit float samples in sound_init, so the buffer is really an array of
// floats. Runs on the audio thread; see the top of this file.
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata; // unused; casting to void tells the compiler that's on purpose
    float *out = (float *)stream;
    int samples = len / (int)sizeof(float);
    float target = audio.muted || audio.paused ? 0.0f : 1.0f;

    for (int i = 0; i < samples; i++) {
        // Fade the overall volume towards silent or full
        if (audio.master_gain < target) {
            audio.master_gain = fminf(target, audio.master_gain + FADE_STEP);
        } else if (audio.master_gain > target) {
            audio.master_gain = fmaxf(target, audio.master_gain - FADE_STEP);
        }
        if (audio.paused && audio.master_gain == 0.0f) {
            out[i] = 0.0f; // paused: don't advance any voice, so each sound holds its place
            continue;
        }

        // Switching loops: fade the old one out, start the new one at silence, fade it in
        if (audio.loop.sound != audio.next_loop) {
            audio.loop_gain -= FADE_STEP;
            if (audio.loop_gain <= 0.0f) {
                audio.loop_gain = 0.0f;
                start_voice(&audio.loop, audio.next_loop);
            }
        } else if (audio.loop_gain < 1.0f) {
            audio.loop_gain = fminf(1.0f, audio.loop_gain + FADE_STEP);
        }

        // Mix: add up every voice
        float mix = voice_sample(&audio.chomp) + audio.loop_gain * voice_sample(&audio.loop);
        for (int v = 0; v < NUM_FX_VOICES; v++) {
            mix += voice_sample(&audio.fx[v]);
        }
        out[i] = audio.master_gain * mix;
    }
}

// ---------------------------------------------------------------------------
// The public functions, on the main thread
// ---------------------------------------------------------------------------

void sound_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("No audio, playing silently: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want;
    SDL_zero(want); // fill with zeros so every field we don't set has a sane value
    want.freq = AUDIO_RATE;
    want.format = AUDIO_F32SYS; // 32-bit floats in this computer's byte order
    want.channels = 1;          // mono: the arcade had one speaker
    want.samples = 512;         // buffer size: about 12 ms of sound per callback
    want.callback = audio_callback;
    // Passing 0 for allowed changes means SDL gives us exactly this format and converts
    // it to whatever the hardware uses, so the callback can rely on it.
    audio.device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio.device == 0) {
        printf("No audio device, playing silently: %s\n", SDL_GetError());
        return;
    }
    audio.master_gain = 1.0f;
    SDL_PauseAudioDevice(audio.device, 0); // devices open paused; 0 starts the callback
}

void sound_quit(void) {
    if (audio.device != 0) {
        SDL_CloseAudioDevice(audio.device);
        audio.device = 0;
    }
}

void sound_play(Sound sound) {
    if (audio.device == 0) {
        return;
    }
    SDL_LockAudioDevice(audio.device);
    if (sound == SOUND_WAKA) {
        // Pellets come about as fast as one half plays, so steady eating gives a
        // continuous waka. A pellet eaten while a half is still playing adds nothing.
        if (audio.chomp.sound == SOUND_NONE) {
            start_voice(&audio.chomp, SOUND_WAKA);
            audio.chomp.half = audio.next_waka_half;
            audio.next_waka_half ^= 1; // flip between 0 and 1
        }
    } else {
        if (sound == SOUND_INTRO || sound == SOUND_DEATH) { // these play alone
            audio.chomp.sound = SOUND_NONE;
            for (int v = 0; v < NUM_FX_VOICES; v++) {
                audio.fx[v].sound = SOUND_NONE;
            }
        }
        // Use a free voice, or else steal the one that has been playing the longest
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

void sound_set_loop(Sound loop, float siren_pitch) {
    // Called every frame, so skip the lock when nothing changed. Reading these without
    // the lock is safe because only this thread ever writes them.
    if (audio.device == 0 || (audio.next_loop == loop && audio.siren_pitch == siren_pitch)) {
        return;
    }
    SDL_LockAudioDevice(audio.device);
    audio.next_loop = loop; // the callback does the actual switch, with a fade
    audio.siren_pitch = siren_pitch;
    SDL_UnlockAudioDevice(audio.device);
}

void sound_set_muted(bool muted) {
    if (audio.device == 0) {
        audio.muted = muted; // no callback is running, so no lock is needed
        return;
    }
    SDL_LockAudioDevice(audio.device);
    audio.muted = muted;
    SDL_UnlockAudioDevice(audio.device);
}

bool sound_is_muted(void) {
    return audio.muted;
}

void sound_set_paused(bool paused) {
    if (audio.device == 0) {
        return;
    }
    SDL_LockAudioDevice(audio.device);
    audio.paused = paused;
    SDL_UnlockAudioDevice(audio.device);
}
