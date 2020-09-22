#pragma once

typedef struct
{
    SDL_atomic_t gain;
    SDL_atomic_t gain_setpoint;
    SDL_atomic_t progress;
    SDL_atomic_t on;
    float _freq_setpoint;
    float _freq;
}
Note;

#define COPY(X) SDL_AtomicSet(&copy.X, SDL_AtomicGet(&note->X))
static Note Note_Copy(Note* note)
{
    Note copy;
    COPY(gain);
    COPY(gain_setpoint);
    COPY(progress);
    COPY(on);
    return copy;;
}
#undef COPY

static float Note_Freq(const float id)
{
    return 440.0f * powf(2.0f, (id - 69.0f) / 12.0f);
}

static float Note_Tick(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq)
{
    const int32_t bend_steps = META_BEND_DEFAULT;
    const int32_t bend = SDL_AtomicGet(&meta->bend[channel]);
    const float bend_id = (bend - bend_steps) / (bend_steps / 12.0f); // XXX. CREATES DISTORTION.
    const int32_t progress = SDL_AtomicGet(&note->progress);
    const float freq = Note_Freq(id + bend_id);
    const float x = progress * (2.0f * META_PI) * freq / sample_freq;
    SDL_AtomicIncRef(&note->progress);
    return x;
}

static int16_t Note_Sin(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq)
{
    const float x = Note_Tick(note, meta, channel, id, sample_freq);
    const int32_t gain = SDL_AtomicGet(&note->gain);
    const float wave = gain * sinf(x);
    return wave;
}

static int16_t Note_Square(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq)
{
    const int16_t wave = Note_Sin(note, meta, channel,id, sample_freq);
    const int32_t gain = SDL_AtomicGet(&note->gain);
    return (wave >= 0 ? gain : -gain) / 10.0f;
}

static int16_t Note_Triangle(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq)
{
    const float x = Note_Tick(note, meta, channel, id, sample_freq);
    const int32_t gain = SDL_AtomicGet(&note->gain);
    const float wave = gain * asinf(sinf(x)) / 1.5708f / 3.0f;
    return wave;
}

static int16_t Note_TriangleHalf(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq)
{
    const int16_t wave = Note_Triangle(note, meta, channel, id, sample_freq);
    return wave > 0 ? (2.0f * wave) : 0;
}

static int16_t Note_SinHalf(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq)
{
    const int16_t wave = Note_Sin(note, meta, channel, id, sample_freq);
    return wave > 0 ? (2.0f * wave) : 0;
}

static int16_t (*NOTE_WAVEFORMS[])(Note* note, Meta* meta, const uint8_t channel, const uint32_t id, const uint32_t sample_freq) = {
    // PIANO.
    [   0 ] = Note_Triangle,
    [   1 ] = Note_Triangle,
    [   2 ] = Note_Triangle,
    [   3 ] = Note_Triangle,
    [   4 ] = Note_Triangle,
    [   5 ] = Note_Triangle,
    [   6 ] = Note_Triangle,
    [   7 ] = Note_Triangle,
    // CHROMATIC PERCUSSION.
    [   8 ] = Note_Sin,
    [   9 ] = Note_Sin,
    [  10 ] = Note_Sin,
    [  11 ] = Note_Sin,
    [  12 ] = Note_Sin,
    [  13 ] = Note_Sin,
    [  14 ] = Note_Sin,
    [  15 ] = Note_Sin,
    // ORGAN.
    [  16 ] = Note_Square,
    [  17 ] = Note_Square,
    [  18 ] = Note_Square,
    [  19 ] = Note_Square,
    [  20 ] = Note_Square,
    [  21 ] = Note_Square,
    [  22 ] = Note_Square,
    [  23 ] = Note_Square,
    // GUITAR.
    [  24 ] = Note_SinHalf,
    [  25 ] = Note_SinHalf,
    [  26 ] = Note_Square,
    [  27 ] = Note_SinHalf,
    [  28 ] = Note_SinHalf,
    [  29 ] = Note_SinHalf,
    [  30 ] = Note_SinHalf,
    [  31 ] = Note_SinHalf,
    // BASS.
    [  32 ] = Note_TriangleHalf,
    [  33 ] = Note_SinHalf,
    [  34 ] = Note_SinHalf,
    [  35 ] = Note_SinHalf,
    [  36 ] = Note_SinHalf,
    [  37 ] = Note_SinHalf,
    [  38 ] = Note_SinHalf,
    [  39 ] = Note_SinHalf,
    // STRINGS.
    [  40 ] = Note_Triangle,
    [  41 ] = Note_Triangle,
    [  42 ] = Note_Triangle,
    [  43 ] = Note_Triangle,
    [  44 ] = Note_Triangle,
    [  45 ] = Note_Triangle,
    [  46 ] = Note_Triangle,
    [  47 ] = Note_Triangle,
    // STRINGS (MORE).
    [  48 ] = Note_Triangle,
    [  49 ] = Note_Triangle,
    [  50 ] = Note_Triangle,
    [  51 ] = Note_Triangle,
    [  52 ] = Note_Triangle,
    [  53 ] = Note_Triangle,
    [  54 ] = Note_Triangle,
    [  55 ] = Note_Triangle,
    // BRASS.
    [  56 ] = Note_Sin,
    [  57 ] = Note_Sin,
    [  58 ] = Note_Sin,
    [  59 ] = Note_Sin,
    [  60 ] = Note_Sin,
    [  61 ] = Note_Sin,
    [  62 ] = Note_Sin,
    [  63 ] = Note_Sin,
    // REED.
    [  64 ] = Note_Square,
    [  65 ] = Note_Square,
    [  66 ] = Note_Square,
    [  67 ] = Note_Square,
    [  68 ] = Note_Square,
    [  69 ] = Note_Square,
    [  70 ] = Note_Square,
    [  71 ] = Note_Square,
    // PIPE.
    [  72 ] = Note_Square,
    [  73 ] = Note_Sin,
    [  74 ] = Note_Square,
    [  75 ] = Note_Square,
    [  76 ] = Note_Square,
    [  77 ] = Note_Square,
    [  78 ] = Note_Square,
    [  79 ] = Note_Square,
    // SYNTH LEAD.
    [  80 ] = Note_Sin,
    [  81 ] = Note_Sin,
    [  82 ] = Note_Triangle,
    [  83 ] = Note_Sin,
    [  84 ] = Note_Sin,
    [  85 ] = Note_Sin,
    [  86 ] = Note_Sin,
    [  87 ] = Note_Sin,
    // SYNTH PAD.
    [  88 ] = Note_Sin,
    [  89 ] = Note_Sin,
    [  90 ] = Note_Sin,
    [  91 ] = Note_Sin,
    [  92 ] = Note_Sin,
    [  93 ] = Note_Sin,
    [  94 ] = Note_Sin,
    [  95 ] = Note_Sin,
    // SYNTH EFFECTS.
    [  96 ] = Note_Sin,
    [  97 ] = Note_Sin,
    [  98 ] = Note_Sin,
    [  99 ] = Note_Sin,
    [ 100 ] = Note_Sin,
    [ 101 ] = Note_Sin,
    [ 102 ] = Note_Sin,
    [ 103 ] = Note_Sin,
    // ETHNIC.
    [ 104 ] = Note_Sin,
    [ 105 ] = Note_Sin,
    [ 106 ] = Note_Sin,
    [ 107 ] = Note_Sin,
    [ 108 ] = Note_Sin,
    [ 109 ] = Note_Sin,
    [ 110 ] = Note_Sin,
    [ 111 ] = Note_Sin,
    // PERCUSSIVE.
    [ 112 ] = Note_Sin,
    [ 113 ] = Note_Sin,
    [ 114 ] = Note_Sin,
    [ 115 ] = Note_Sin,
    [ 116 ] = Note_Sin,
    [ 117 ] = Note_Sin,
    [ 118 ] = Note_Sin,
    [ 119 ] = Note_Sin,
    // SOUND EFFECTS.
    [ 120 ] = Note_Sin,
    [ 121 ] = Note_Sin,
    [ 122 ] = Note_Sin,
    [ 123 ] = Note_Sin,
    [ 124 ] = Note_Sin,
    [ 125 ] = Note_Sin,
    [ 126 ] = Note_Sin,
    [ 127 ] = Note_Sin,
};

static void Note_Clamp(Note* note)
{
    const int32_t min = 0;
    const int32_t max = META_NOTE_SUSTAIN * 127;
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
