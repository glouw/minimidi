#pragma once

#define NOTE_AMPLIFICATION (20)

typedef struct
{
    SDL_atomic_t gain;
    SDL_atomic_t gain_setpoint;
    SDL_atomic_t progress;
    SDL_atomic_t on;
}
Note;

static int16_t Note_Sin(Note* note, const uint32_t id, const uint32_t sample_freq)
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

static int16_t Note_SinHalf(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    const int16_t wave = Note_Sin(note, id, sample_freq);
    return wave > 0 ? wave : 0;
}

static int16_t Note_SinAbs(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    return abs(Note_Sin(note, id, sample_freq));
}

static int16_t Note_SinQuarter(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    Note copy = *note;
    SDL_AtomicDecRef(&copy.progress);
    const int16_t a = Note_SinAbs(&copy, id, sample_freq);
    const int16_t b = Note_SinAbs( note, id, sample_freq);
    return b > a ? b : 0;
}

static int16_t (*NOTE_WAVEFORMS[])(Note* note, const uint32_t id, const uint32_t sample_freq) = {
    [   0 ] = Note_SinHalf,
    [   1 ] = Note_SinHalf,
    [   2 ] = Note_SinHalf,
    [   3 ] = Note_SinHalf,
    [   4 ] = Note_SinHalf,
    [   5 ] = Note_SinHalf,
    [   6 ] = Note_SinHalf,
    [   7 ] = Note_SinHalf,
    [   8 ] = Note_SinHalf,
    [   9 ] = Note_SinHalf,
    [  10 ] = Note_SinHalf,
    [  11 ] = Note_SinHalf,
    [  12 ] = Note_SinHalf,
    [  13 ] = Note_SinHalf,
    [  14 ] = Note_SinHalf,
    [  15 ] = Note_SinHalf,
    [  16 ] = Note_SinHalf,
    [  17 ] = Note_SinHalf,
    [  18 ] = Note_SinHalf,
    [  19 ] = Note_SinHalf,
    [  20 ] = Note_SinHalf,
    [  21 ] = Note_SinHalf,
    [  22 ] = Note_SinHalf,
    [  23 ] = Note_SinHalf,
    [  24 ] = Note_SinHalf,
    [  25 ] = Note_SinHalf,
    [  26 ] = Note_SinHalf,
    [  27 ] = Note_SinHalf,
    [  28 ] = Note_SinHalf,
    [  29 ] = Note_SinHalf,
    [  30 ] = Note_SinHalf,
    [  31 ] = Note_SinHalf,
    [  32 ] = Note_SinHalf,
    [  33 ] = Note_SinHalf,
    [  34 ] = Note_SinHalf,
    [  35 ] = Note_SinHalf,
    [  36 ] = Note_SinHalf,
    [  37 ] = Note_SinHalf,
    [  38 ] = Note_SinHalf,
    [  39 ] = Note_SinHalf,
    [  40 ] = Note_SinHalf,
    [  41 ] = Note_SinHalf,
    [  42 ] = Note_SinHalf,
    [  43 ] = Note_SinHalf,
    [  44 ] = Note_SinHalf,
    [  45 ] = Note_SinHalf,
    [  46 ] = Note_SinHalf,
    [  47 ] = Note_SinHalf,
    [  48 ] = Note_SinHalf,
    [  49 ] = Note_SinHalf,
    [  50 ] = Note_SinHalf,
    [  51 ] = Note_SinHalf,
    [  52 ] = Note_SinHalf,
    [  53 ] = Note_SinHalf,
    [  54 ] = Note_SinHalf,
    [  55 ] = Note_SinHalf,
    [  56 ] = Note_SinHalf,
    [  57 ] = Note_SinHalf,
    [  58 ] = Note_SinHalf,
    [  59 ] = Note_SinHalf,
    [  60 ] = Note_SinHalf,
    [  61 ] = Note_SinHalf,
    [  62 ] = Note_SinHalf,
    [  63 ] = Note_SinHalf,
    [  64 ] = Note_SinHalf,
    [  65 ] = Note_SinHalf,
    [  66 ] = Note_SinHalf,
    [  67 ] = Note_SinHalf,
    [  68 ] = Note_SinHalf,
    [  69 ] = Note_SinHalf,
    [  70 ] = Note_SinHalf,
    [  71 ] = Note_SinHalf,
    [  72 ] = Note_SinHalf,
    [  73 ] = Note_SinHalf,
    [  74 ] = Note_SinHalf,
    [  75 ] = Note_SinHalf,
    [  76 ] = Note_SinHalf,
    [  77 ] = Note_SinHalf,
    [  78 ] = Note_SinHalf,
    [  79 ] = Note_SinHalf,
    [  80 ] = Note_SinHalf,
    [  81 ] = Note_SinHalf,
    [  82 ] = Note_SinHalf,
    [  83 ] = Note_SinHalf,
    [  84 ] = Note_SinHalf,
    [  85 ] = Note_SinHalf,
    [  86 ] = Note_SinHalf,
    [  87 ] = Note_SinHalf,
    [  88 ] = Note_SinHalf,
    [  89 ] = Note_SinHalf,
    [  90 ] = Note_SinHalf,
    [  91 ] = Note_SinHalf,
    [  92 ] = Note_SinHalf,
    [  93 ] = Note_SinHalf,
    [  94 ] = Note_SinHalf,
    [  95 ] = Note_SinHalf,
    [  96 ] = Note_SinHalf,
    [  97 ] = Note_SinHalf,
    [  98 ] = Note_SinHalf,
    [  99 ] = Note_SinHalf,
    [ 100 ] = Note_SinHalf,
    [ 101 ] = Note_SinHalf,
    [ 102 ] = Note_SinHalf,
    [ 103 ] = Note_SinHalf,
    [ 104 ] = Note_SinHalf,
    [ 105 ] = Note_SinHalf,
    [ 106 ] = Note_SinHalf,
    [ 107 ] = Note_SinHalf,
    [ 108 ] = Note_SinHalf,
    [ 109 ] = Note_SinHalf,
    [ 110 ] = Note_SinHalf,
    [ 111 ] = Note_SinHalf,
    [ 112 ] = Note_SinHalf,
    [ 113 ] = Note_SinHalf,
    [ 114 ] = Note_SinHalf,
    [ 115 ] = Note_SinHalf,
    [ 116 ] = Note_SinHalf,
    [ 117 ] = Note_SinHalf,
    [ 118 ] = Note_SinHalf,
    [ 119 ] = Note_SinHalf,
    [ 120 ] = Note_SinHalf,
    [ 121 ] = Note_SinHalf,
    [ 122 ] = Note_SinHalf,
    [ 123 ] = Note_SinHalf,
    [ 124 ] = Note_SinHalf,
    [ 125 ] = Note_SinHalf,
    [ 126 ] = Note_SinHalf,
    [ 127 ] = Note_SinHalf,
};

static void Note_Clamp(Note* note)
{
    const int32_t min = 0;
    const int32_t max = NOTE_AMPLIFICATION * 127;
    const int32_t gain = SDL_AtomicGet(&note->gain);
    if(gain < min) SDL_AtomicSet(&note->gain, min);
    if(gain > max) SDL_AtomicSet(&note->gain, max);
}

static void Note_Roll(Note* note)
{
    const int32_t gain = SDL_AtomicGet(&note->gain);
    const int32_t gain_setpoint = SDL_AtomicGet(&note->gain_setpoint);
    const int32_t diff = gain_setpoint - gain;
    const int32_t decay = 200;
    if(diff == 0)
    {
        if(gain == 0)
            SDL_AtomicSet(&note->on, false);
        else // NOTE DECAY WHEN HELD.
        {
            const int32_t progress = SDL_AtomicGet(&note->progress);
            if(progress != 0 && (progress % decay == 0))
            {
                SDL_AtomicDecRef(&note->gain);
                SDL_AtomicDecRef(&note->gain_setpoint);
            }
        }
    }
    else // NOTE DELTA RAMP - PREVENTS CLICKS AND POPS.
    {
        const int32_t step = diff / abs(diff);
        SDL_AtomicAdd(&note->gain, step);
    }
}
