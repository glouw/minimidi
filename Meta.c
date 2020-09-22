#pragma once

#define META_NOTE_SUSTAIN (4)

#define META_NOTES_MAX (128)

#define META_CHANNEL_MAX (16)

#define META_AUDIO_AMPLIFICATION (4)

#define META_BEND_DEFAULT (8192)

#define META_PI (3.14159265358979323846f)

typedef struct
{
    uint32_t tempo;
    SDL_atomic_t instruments[META_CHANNEL_MAX];
    SDL_atomic_t bend[META_CHANNEL_MAX];
}
Meta;
