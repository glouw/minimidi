#pragma once

typedef struct
{
    Note* note;
    Meta* meta;
    uint8_t channel;
    uint32_t id;
    uint32_t sample_freq;
}
Wave;

static float Note_Freq(Note* note)
{
    return 440.0f * powf(2.0f, (note->_id - 69.0f) / 12.0f);
}

static float Wave_X(Wave* wave, const float progress)
{
    const float freq = Note_Freq(wave->note);
    return (progress * (2.0f * META_PI) * freq) / wave->sample_freq;
}

static float Wave_Tick(Wave* wave)
{
    const int32_t bend = SDL_AtomicGet(&wave->meta->bend[wave->channel]);
    if(!wave->note->_was_init)
    {
        wave->note->_was_init = true;
        wave->note->_id = wave->id;
    }
    if(bend != wave->note->_bend_last)
    {
        wave->note->_bend_last = bend;
        wave->note->_wait = true;
    }
    if(wave->note->_wait)
    {
        const int32_t progress = SDL_AtomicGet(&wave->note->progress);
        const float x0 = Wave_X(wave, progress - 0.2f);
        const float x1 = Wave_X(wave, progress + 0.0f);
        const int32_t gain = SDL_AtomicGet(&wave->note->gain);
        const float a = gain * sinf(x0);
        const float b = gain * sinf(x1);
        const bool crossed = a < 0.0f && b > 0.0f;
        if(crossed) // NOTE FREQUENCY CAN ONLY BE CHANGED AT X AXIS CROSSING.
        {
            const float bend_semitones = 12.0f;
            const float bend_id = (bend - META_BEND_DEFAULT) / (META_BEND_DEFAULT / bend_semitones);
            wave->note->_id = bend_id + wave->id;
            wave->note->_wait = false;
            SDL_AtomicSet(&wave->note->progress, 0);
        }
    }
    const int32_t progress = SDL_AtomicGet(&wave->note->progress);
    const float x = Wave_X(wave, progress);
    SDL_AtomicIncRef(&wave->note->progress);
    return x;
}

static int16_t Wave_Sin(Wave* wave)
{
    const float x = Wave_Tick(wave);
    const int32_t gain = SDL_AtomicGet(&wave->note->gain);
    return gain * sinf(x);
}

static int16_t Wave_Square(Wave* wave)
{
    const int16_t amp = Wave_Sin(wave);
    const int32_t gain = SDL_AtomicGet(&wave->note->gain);
    return (amp >= 0 ? gain : -gain) / 10;
}

static int16_t Wave_Triangle(Wave* wave)
{
    const float x = Wave_Tick(wave);
    const int32_t gain = SDL_AtomicGet(&wave->note->gain);
    return gain * asinf(sinf(x)) / 1.5708f / 3;
}

static int16_t Wave_TriangleHalf(Wave* wave)
{
    const int16_t amp = Wave_Triangle(wave);
    return amp > 0 ? (2 * amp) : 0;
}

static int16_t Wave_SinHalf(Wave* wave)
{
    const int16_t amp = Wave_Sin(wave);
    return amp > 0 ? (2 * amp) : 0;
}

static int16_t (*WAVE_WAVEFORMS[])(Wave* wave) = {
    // PIANO.
    [   0 ] = Wave_Triangle,
    [   1 ] = Wave_Triangle,
    [   2 ] = Wave_Triangle,
    [   3 ] = Wave_Triangle,
    [   4 ] = Wave_Triangle,
    [   5 ] = Wave_Triangle,
    [   6 ] = Wave_Triangle,
    [   7 ] = Wave_Triangle,
    // CHROMATIC PERCUSSION.
    [   8 ] = Wave_Sin,
    [   9 ] = Wave_Sin,
    [  10 ] = Wave_Sin,
    [  11 ] = Wave_Sin,
    [  12 ] = Wave_Sin,
    [  13 ] = Wave_Sin,
    [  14 ] = Wave_Sin,
    [  15 ] = Wave_Sin,
    // ORGAN.
    [  16 ] = Wave_Square,
    [  17 ] = Wave_Square,
    [  18 ] = Wave_Square,
    [  19 ] = Wave_Square,
    [  20 ] = Wave_Square,
    [  21 ] = Wave_Square,
    [  22 ] = Wave_Square,
    [  23 ] = Wave_Square,
    // GUITAR.
    [  24 ] = Wave_SinHalf,
    [  25 ] = Wave_SinHalf,
    [  26 ] = Wave_Square, // GOOD.
    [  27 ] = Wave_SinHalf,
    [  28 ] = Wave_SinHalf,
    [  29 ] = Wave_SinHalf,
    [  30 ] = Wave_SinHalf,
    [  31 ] = Wave_SinHalf,
    // BASS.
    [  32 ] = Wave_TriangleHalf,
    [  33 ] = Wave_SinHalf, // GOOD
    [  34 ] = Wave_SinHalf,
    [  35 ] = Wave_SinHalf,
    [  36 ] = Wave_SinHalf,
    [  37 ] = Wave_SinHalf,
    [  38 ] = Wave_SinHalf,
    [  39 ] = Wave_SinHalf,
    // STRINGS.
    [  40 ] = Wave_Triangle,
    [  41 ] = Wave_Triangle,
    [  42 ] = Wave_Triangle,
    [  43 ] = Wave_Triangle,
    [  44 ] = Wave_Triangle,
    [  45 ] = Wave_Triangle,
    [  46 ] = Wave_Triangle,
    [  47 ] = Wave_SinHalf,
    // STRINGS (MORE).
    [  48 ] = Wave_Triangle, // GOOD.
    [  49 ] = Wave_Triangle,
    [  50 ] = Wave_TriangleHalf,
    [  51 ] = Wave_Triangle,
    [  52 ] = Wave_Triangle,
    [  53 ] = Wave_Triangle,
    [  54 ] = Wave_Triangle,
    [  55 ] = Wave_Triangle,
    // BRASS.
    [  56 ] = Wave_TriangleHalf,
    [  57 ] = Wave_TriangleHalf,
    [  58 ] = Wave_TriangleHalf,
    [  59 ] = Wave_TriangleHalf,
    [  60 ] = Wave_TriangleHalf,
    [  61 ] = Wave_TriangleHalf,
    [  62 ] = Wave_TriangleHalf,
    [  63 ] = Wave_TriangleHalf,
    // REED.
    [  64 ] = Wave_Square,
    [  65 ] = Wave_Square,
    [  66 ] = Wave_Square,
    [  67 ] = Wave_Square,
    [  68 ] = Wave_Square,
    [  69 ] = Wave_Square,
    [  70 ] = Wave_Square,
    [  71 ] = Wave_Square,
    // PIPE.
    [  72 ] = Wave_Sin,
    [  73 ] = Wave_Sin, // GOOD.
    [  74 ] = Wave_Sin,
    [  75 ] = Wave_Sin,
    [  76 ] = Wave_Sin,
    [  77 ] = Wave_Sin,
    [  78 ] = Wave_Sin,
    [  79 ] = Wave_Sin,
    // SYNTH LEAD.
    [  80 ] = Wave_Sin,
    [  81 ] = Wave_Sin,
    [  82 ] = Wave_Triangle,
    [  83 ] = Wave_Sin,
    [  84 ] = Wave_Sin,
    [  85 ] = Wave_Sin,
    [  86 ] = Wave_Sin,
    [  87 ] = Wave_Sin,
    // SYNTH PAD.
    [  88 ] = Wave_Sin,
    [  89 ] = Wave_Sin,
    [  90 ] = Wave_Sin,
    [  91 ] = Wave_Sin,
    [  92 ] = Wave_Sin,
    [  93 ] = Wave_Sin,
    [  94 ] = Wave_Sin,
    [  95 ] = Wave_Sin,
    // SYNTH EFFECTS.
    [  96 ] = Wave_Sin,
    [  97 ] = Wave_Sin,
    [  98 ] = Wave_Sin,
    [  99 ] = Wave_Sin,
    [ 100 ] = Wave_Sin,
    [ 101 ] = Wave_Sin,
    [ 102 ] = Wave_Sin,
    [ 103 ] = Wave_Sin,
    // ETHNIC.
    [ 104 ] = Wave_Sin,
    [ 105 ] = Wave_Sin,
    [ 106 ] = Wave_Sin,
    [ 107 ] = Wave_Sin,
    [ 108 ] = Wave_Sin,
    [ 109 ] = Wave_Sin,
    [ 110 ] = Wave_Sin,
    [ 111 ] = Wave_Sin,
    // PERCUSSIVE.
    [ 112 ] = Wave_Sin,
    [ 113 ] = Wave_Sin,
    [ 114 ] = Wave_Sin,
    [ 115 ] = Wave_Sin,
    [ 116 ] = Wave_Sin,
    [ 117 ] = Wave_Sin,
    [ 118 ] = Wave_Sin,
    [ 119 ] = Wave_Sin,
    // SOUND EFFECTS.
    [ 120 ] = Wave_Sin,
    [ 121 ] = Wave_Sin,
    [ 122 ] = Wave_Sin,
    [ 123 ] = Wave_Sin,
    [ 124 ] = Wave_Sin,
    [ 125 ] = Wave_Sin,
    [ 126 ] = Wave_Sin,
    [ 127 ] = Wave_Sin,
};
