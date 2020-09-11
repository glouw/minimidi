#pragma once

typedef struct
{
    SDL_atomic_t gain;
    SDL_atomic_t gain_setpoint;
    SDL_atomic_t progress;
    SDL_atomic_t channel;
}
Note;

static const uint32_t NOTE_AMPLIFICATION = 5;

static int16_t
(Note_Sin)
(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    const int32_t progress = SDL_AtomicGet(&note->progress);
    const float pi = 3.14159265358979323846f;
    const float freq = 440.0f * powf(2.0f, (id - 69.0f) / 12.0f);
    const float x = progress * (2 * pi) * freq / sample_freq;
    const int32_t gain = SDL_AtomicGet(&note->gain);
    const float wave = gain * sinf(x);
    SDL_AtomicIncRef(&note->progress);
    return wave;
}

static int16_t
(Note_SinHalf)
(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    const int16_t wave = Note_Sin(note, id, sample_freq);
    return wave > 0 ? wave : 0;
}

static int16_t
(Note_SinAbs)
(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    return abs(Note_Sin(note, id, sample_freq));
}

static int16_t
(Note_SinQuarter)
(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    Note copy = *note;
    SDL_AtomicDecRef(&copy.progress);
    const int16_t a = Note_SinAbs(&copy, id, sample_freq);
    const int16_t b = Note_SinAbs( note, id, sample_freq);
    return b > a ? b : 0;
}

static int16_t
(Note_Square)
(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    const int16_t wave = Note_Sin(note, id, sample_freq);
    const int32_t gain = SDL_AtomicGet(&note->gain);
    return gain * (wave > 0 ? 1 : -1);
}

static int16_t
(*NOTE_INTRUMENTS[])
(Note* note, const uint32_t id, const uint32_t sample_freq) = {
    [0] = Note_Sin,
    [1] = Note_SinHalf,
    [2] = Note_SinAbs,
    [3] = Note_SinQuarter,
    [4] = /* TEMP */ Note_Sin,
    [5] = /* TEMP */ Note_Sin,
    [6] = /* TEMP */ Note_Sin,
    [7] = Note_Square,
};

static const uint32_t NOTE_MAX_INSTRUMENTS = sizeof(NOTE_INTRUMENTS) / sizeof(NOTE_INTRUMENTS[0]);

static void
(Note_On)
(Note* note, const uint8_t note_velocity, const uint8_t channel)
{
    SDL_AtomicSet(&note->gain_setpoint, NOTE_AMPLIFICATION * note_velocity);
    SDL_AtomicSet(&note->channel, channel);
}

static void
(Note_Clamp)
(Note* note)
{
    const int32_t min = 0;
    const int32_t max = NOTE_AMPLIFICATION * 127;
    const int32_t gain = SDL_AtomicGet(&note->gain);
    if(gain < min) SDL_AtomicSet(&note->gain, min);
    if(gain > max) SDL_AtomicSet(&note->gain, max);
}

static void
(Note_Roll)
(Note* note)
{
    const int32_t gain = SDL_AtomicGet(&note->gain);
    const int32_t gain_setpoint = SDL_AtomicGet(&note->gain_setpoint);
    const int32_t diff = gain_setpoint - gain;
    const int32_t decay = 200;
    if(diff == 0) // NOTE DECAY WHEN HELD.
    {
        const int32_t progress = SDL_AtomicGet(&note->progress);
        if(progress != 0 && (progress % decay == 0))
        {
            SDL_AtomicDecRef(&note->gain);
            SDL_AtomicDecRef(&note->gain_setpoint);
        }
    }
    else // NOTE DELTA RAMP - PREVENTS CLICKS AND POPS.
    {
        const int32_t step = diff / abs(diff);
        SDL_AtomicAdd(&note->gain, step);
    }
}
