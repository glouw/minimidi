#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

typedef enum
{
    CONST_NOTE_SUSTAIN = 4,
    CONST_NOTES_MAX = 128,
    CONST_CHANNEL_MAX = 16,
    CONST_BEND_DEFAULT = 8192,
}
Const;

enum
{
    ERROR_NONE,
    ERROR_ARGC,
    ERROR_FILE,
    ERROR_CRASH
}
Error;

typedef struct
{
    uint32_t tempo;
    int instruments[CONST_CHANNEL_MAX];
    int bend[CONST_CHANNEL_MAX];
}
Meta;

typedef struct
{
    uint8_t* data;
    uint32_t size;
}
Bytes;

typedef struct
{
    int gain;
    int gain_setpoint;
    int progress;
    int on;
    // PRIVATE.
    int32_t bend_last;
    float id;
    bool wait;
    bool was_init;
}
Note;

typedef struct
{
    Note note[CONST_NOTES_MAX][CONST_CHANNEL_MAX];
    uint32_t display_rows;
    uint32_t display_cols;
}
Notes;

typedef struct
{
    Note* note;
    Meta* meta;
    uint8_t channel;
    uint32_t id;
    uint32_t sample_freq;
}
Wave;

typedef struct
{
    FILE* file;
    bool loop;
}
Args;

typedef struct
{
    uint8_t* data;
    uint32_t id;
    uint32_t size;
    uint32_t index;
    uint32_t number;
    int64_t delay;
    uint8_t running_status;
    bool run;
}
Track;

typedef struct
{
    SDL_AudioSpec spec;
    SDL_AudioDeviceID dev;
    int done;
}
Audio;

typedef struct
{
    Audio* audio;
    Notes* notes;
    Meta* meta;
}
AudioConsumer;

typedef struct
{
    Track* track;
    uint32_t id;
    uint32_t size;
    uint16_t format_type;
    uint16_t track_count;
    uint16_t time_division;
}
Midi;

static Bytes Bytes_FromFile(FILE* file)
{
    fseek(file, 0, SEEK_END);
    const uint32_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    Bytes bytes = { 0 };
    bytes.size = size;
    bytes.data = calloc(bytes.size, sizeof(*bytes.data));
    for(uint32_t i = 0; i < bytes.size; i++)
        bytes.data[i] = getc(file);
    return bytes;
}

static void Bytes_Free(Bytes* bytes)
{
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}

static uint8_t Bytes_U8(Bytes* bytes, const uint32_t index)
{
    return bytes->data[index];
}

static uint16_t Bytes_U16(Bytes* bytes, const uint32_t index)
{
    return Bytes_U8(bytes, index + 0) << 8 | Bytes_U8(bytes, index + 1);
}

static uint32_t Bytes_U32(Bytes* bytes, const uint32_t index)
{
    return Bytes_U16(bytes, index + 0) << 16 | Bytes_U16(bytes, index + 2);
}

static void Note_Clamp(Note* note)
{
    const int32_t min = 0;
    const int32_t max = CONST_NOTE_SUSTAIN * 127;
    if(note->gain < min) note->gain = min;
    if(note->gain > max) note->gain = max;
}

static void Note_Roll(Note* note)
{
    const int32_t diff = note->gain_setpoint - note->gain;
    const int32_t decay = 200;
    if(diff == 0)
    {
        if(note->gain == 0)
        {
            note->was_init = false;
            note->on = false;
        }
        else // NOTE DECAY WHEN HELD.
        {
            if(note->progress != 0 && (note->progress % decay == 0))
            {
                note->gain -= 1;
                note->gain_setpoint -= 1;
            }
        }
    }
    else // NOTE DELTA RAMP - PREVENTS CLICKS AND POPS.
    {
        const int32_t step = diff / abs(diff);
        note->gain += step;
    }
}

static Notes Notes_Init(void)
{
    Notes notes = { 0 };
    notes.display_rows = 32;
    notes.display_cols = CONST_NOTES_MAX / notes.display_rows;
    return notes;
}

static void Notes_Free(Notes* notes)
{
    // PATCH UP DISPLAY FROM CLIPPING WITH PROMPT AT EXIT.
    for(uint32_t i = 0; i < notes->display_rows; i++)
        putchar('\n');
}

static void Notes_Draw(Notes* notes, Meta* meta)
{
    const char* const grn = "\x1b[0;32m";
    const char* const red = "\x1b[0;31m";
    const char* const nrm = "\x1b[0;00m";
    int32_t note_index = 0;
    const int32_t draw_per_channel = 4;
    for(uint32_t y = 0; y < notes->display_rows; y++)
    {
        for(uint32_t x = 0; x < notes->display_cols; x++)
        {
            bool audible = false;
            for(uint8_t channel = 0; channel < CONST_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[note_index][channel];
                audible = note->gain > 0;
                if(audible)
                    break;
            }
            printf("%s%4d", audible ? grn : red, note_index);
            int32_t got = 0;
            for(uint8_t channel = 0; channel < CONST_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[note_index][channel];
                const bool draw = note->gain > 0;
                if(draw)
                {
                    const uint8_t instrument = meta->instruments[channel];
                    printf("%4d:%3X:%1X", instrument, note->gain, channel);
                    got += 1;
                }
            }
            for(int i = 0; i < draw_per_channel - got; i++)
                printf("%10s", "");
            note_index += 1;
        }
        putchar('\n');
    }
    // GO UP BY NUMBER OF DISPLAY ROWS AND LEFTMOST COLUMN.
    printf("\x1B[%dA", notes->display_rows);
    printf("\r");
    printf(nrm);
}

static float Note_Freq(Note* note)
{
    return 440.0f * powf(2.0f, (note->id - 69.0f) / 12.0f);
}

static float Wave_X(Wave* wave, const float progress)
{
    const float freq = Note_Freq(wave->note);
    const float pi = 3.14159265358979323846f;
    return (progress * (2.0f * pi) * freq) / wave->sample_freq;
}

static float Wave_Tick(Wave* wave)
{
    const int32_t bend = wave->meta->bend[wave->channel];
    if(!wave->note->was_init)
    {
        wave->note->was_init = true;
        wave->note->id = wave->id;
    }
    if(bend != wave->note->bend_last)
    {
        wave->note->bend_last = bend;
        wave->note->wait = true;
    }
    if(wave->note->wait)
    {
        const float x0 = Wave_X(wave, wave->note->progress - 0.2f);
        const float x1 = Wave_X(wave, wave->note->progress + 0.0f);
        const float a = wave->note->gain * sinf(x0);
        const float b = wave->note->gain * sinf(x1);
        const bool crossed = a < 0.0f && b > 0.0f;
        if(crossed) // NOTE FREQUENCY CAN ONLY BE CHANGED AT X AXIS CROSSING.
        {
            const float bend_semitones = 12.0f;
            const float bendid = (bend - CONST_BEND_DEFAULT) / (CONST_BEND_DEFAULT / bend_semitones);
            wave->note->id = bendid + wave->id;
            wave->note->wait = false;
            wave->note->progress = 0;
        }
    }
    const float x = Wave_X(wave, wave->note->progress);
    wave->note->progress += 1;
    return x;
}

static int16_t Wave_Sin(Wave* wave)
{
    const float x = Wave_Tick(wave);
    return wave->note->gain * sinf(x);
}

static int16_t Wave_Square(Wave* wave)
{
    const int16_t amp = Wave_Sin(wave);
    return (amp >= 0 ? wave->note->gain : -wave->note->gain) / 10;
}

static int16_t Wave_Triangle(Wave* wave)
{
    const float x = Wave_Tick(wave);
    return wave->note->gain * asinf(sinf(x)) / 1.5708f / 3;
}

static int16_t Wave_TriangleHalf(Wave* wave)
{
    const int16_t amp = Wave_Triangle(wave);
    return amp > 0 ? (2.0f * amp) : 0;
}

static int16_t Wave_SinHalf(Wave* wave)
{
    const int16_t amp = Wave_Sin(wave);
    return amp > 0 ? (1.2f * amp) : 0;
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
    [   8 ] = Wave_Square,
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
    [  40 ] = Wave_TriangleHalf,
    [  41 ] = Wave_Triangle,
    [  42 ] = Wave_Triangle,
    [  43 ] = Wave_Triangle,
    [  44 ] = Wave_Triangle,
    [  45 ] = Wave_Triangle,
    [  46 ] = Wave_Square,
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

static Args Args_Init(const int argc, char** argv)
{
    Args args = { 0 };
    args.loop = false;
    args.file = NULL;
    if(argc < 2 || argc > 3)
    {
        puts("./minimidi <file> <loop [0, 1]>");
        exit(ERROR_ARGC);
    }
    if(argc >= 2)
        args.file = fopen(argv[1], "rb");
    if(args.file == NULL)
        exit(ERROR_FILE);
    if(argc == 3)
        args.loop = atoi(argv[2]) == 1;
    return args;
}

static void Args_Free(Args* args)
{
    fclose(args->file);
}

static void Track_Free(Track* track)
{
    free(track->data);
    track->data = NULL;
}

static void Track_Back(Track* track)
{
    track->index -= 1;
}

static void Track_Next(Track* track)
{
    track->index += 1;
}

static uint8_t Track_U8(Track* track)
{
    const uint8_t byte = track->data[track->index];
    Track_Next(track);
    return byte;
}

static uint32_t Track_Var(Track* track)
{
    uint32_t var = 0x0;
    bool run = true;
    while(run)
    {
        const uint8_t byte = Track_U8(track);
        var = (var << 7) | (byte & 0x7F);
        run = byte >> 7;
    }
    return var;
}

static char* Track_Str(Track* track)
{
    const uint32_t len = Track_U8(track);
    char* const str = calloc(len + 1, sizeof(*str));
    for(uint32_t i = 0; i < len; i++)
        str[i] = Track_U8(track);
    return str;
}

static Bytes Track_Bytes(Track* track)
{
    Bytes bytes = { 0 };
    bytes.size = Track_Var(track);
    bytes.data = calloc(bytes.size, sizeof(*bytes.data));
    for(uint32_t i = 0; i < bytes.size; i++)
        bytes.data[i] = Track_U8(track);
    return bytes;
}

static void Track_Crash(Track* track)
{
    const int32_t window = 30;
    for(int32_t i = -window; i < window; i++)
    {
        const uint32_t j = track->index + i;
        if(j < track->size)
        {
            const char* const star = (j == track->index) ? "*" : "";
            fprintf(stderr, "index %d : 0x%02X%s\n", j, track->data[j], star);
        }
    }
    exit(ERROR_CRASH);
}

static uint8_t Track_Status(Track* track, const uint8_t status)
{
    if(status >> 3)
    {
        track->running_status = status;
        return status;
    }
    else
    {
        Track_Back(track);
        return track->running_status;
    }
}

static bool Notes_IsPercussive(const uint8_t channel)
{
    return channel == 9;
}

static void Track_RealEvent(Track* track, Meta* meta, Notes* notes, const uint8_t leader)
{
    const uint8_t channel = leader & 0xF;
    const uint8_t status = leader >> 4;
    switch(Track_Status(track, status))
    {
        default:
        {
            Track_Crash(track);
            break;
        }
        case 0x8: // NOTE OFF.
        {
            const uint8_t note_index = Track_U8(track);
            Track_U8(track);
            if(!Notes_IsPercussive(channel))
            {
                Note* note = &notes->note[note_index][channel];
                note->gain_setpoint = 0;
                meta->bend[channel] = CONST_BEND_DEFAULT;
            }
            break;
        }
        case 0x9: // NOTE ON.
        {
            const uint8_t note_index = Track_U8(track);
            const uint8_t note_velocity = Track_U8(track);
            if(!Notes_IsPercussive(channel))
            {
                Note* note = &notes->note[note_index][channel];
                note->gain_setpoint = CONST_NOTE_SUSTAIN * note_velocity;
                note->on = true;
                meta->bend[channel] = CONST_BEND_DEFAULT;
            }
            break;
        }
        case 0xA: // NOTE AFTERTOUCH.
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0xB: // CONTROLLER.
        {
            const uint8_t type = Track_U8(track);
            const uint8_t value = Track_U8(track);
            switch(type)
            {
                case 0x07: // CHANNEL VOLUME.
                {
                    for(uint32_t note_index = 0; note_index < CONST_NOTES_MAX; note_index++)
                    {
                        Note* note = &notes->note[note_index][channel];
                        const bool audible = note->gain_setpoint > 0;
                        if(audible)
                            note->gain_setpoint = CONST_NOTE_SUSTAIN * value;
                    }
                    break;
                }
            }
            break;
        }
        case 0xC: // PROGRAM CHANGE.
        {
            const uint8_t program = Track_U8(track);
            meta->instruments[channel] = program;
            break;
        }
        case 0xD: // CHANNEL AFTERTOUCH.
        {
            Track_U8(track);
            break;
        }
        case 0xE: // PITCH BEND.
        {
            const uint8_t lsb = Track_U8(track);
            const uint8_t msb = Track_U8(track);
            const uint16_t bend = (msb << 7) | lsb;
            meta->bend[channel] = bend;
            break;
        }
        case 0xF: // SYSEX START (CASIO).
        {
            Bytes bytes = Track_Bytes(track);
            Bytes_Free(&bytes);
            break;
        }
    }
}

static void Track_MetaEvent(Track* track, Meta* meta)
{
    switch(Track_U8(track))
    {
        default:
        {
            Track_Crash(track);
            break;
        }
        case 0x00: // SEQUENCE NUMBER.
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x01: // TEXT EVENT.
        case 0x02: // COPYRIGHT NOTICE.
        case 0x03: // TRACK NAME.
        case 0x04: // INSTRUMENT NAME.
        case 0x05: // LYRIC.
        case 0x06: // MARKER.
        case 0x07: // CUE POINT.
        case 0x08: // PROGRAM NAME.
        case 0x09: // DEVICE NAME.
        {
            free(Track_Str(track));
            break;
        }
        case 0x20: // CHANNEL PREFIX.
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x21: // MIDI PORT.
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x2F: // END OF TRACK.
        {
            track->run = Track_U8(track);
            break;
        }
        case 0x51: // TEMPO.
        {
            Track_U8(track);
            const uint8_t a = Track_U8(track);
            const uint8_t b = Track_U8(track);
            const uint8_t c = Track_U8(track);
            meta->tempo = (a << 16) | (b << 8) | c;
            break;
        }
        case 0X54: // SMPTE OFFSET.
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x58: // TIME SIGNATURE.
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x59: // KEY SIGNATURE.
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0xF0: // SYSEX START.
        case 0xF7: // SYSEX END.
        case 0x7F: // SYSTEX RUNNING STATUS.
        {
            Bytes bytes = Track_Bytes(track);
            Bytes_Free(&bytes);
            break;
        }
    }
}

static void Track_Play(Track* track, Notes* notes, Meta* meta)
{
    const int32_t end = -1;
    if(track->run)
    {
        if(track->delay > end)
            track->delay -= 1;
        if(track->delay == end)
            track->delay = Track_Var(track);
        if(track->delay == 0)
        {
            const uint8_t leader = Track_U8(track);
            leader == 0xFF
                ? Track_MetaEvent(track, meta)
                : Track_RealEvent(track, meta, notes, leader);
            // NOTES WITH ZERO DELAY MUST IMMEDIATELY PROCESS
            // THE NEXT NOTE BEFORE MOVING ONTO THE NEXT TRACK.
            Track_Play(track, notes, meta);
        }
    }
}

static Track Track_Init(Bytes* bytes, const uint32_t offset, const uint32_t number)
{
    Track track = { 0 };
    track.id = Bytes_U32(bytes, offset);
    track.size = Bytes_U32(bytes, offset + 4);
    track.data = calloc(track.size, sizeof(*track.data));
    track.run = true;
    track.number = number;
    for(uint32_t i = 0; i < track.size; i++)
        track.data[i] = Bytes_U8(bytes, offset + 8 + i);
    return track;
}

static Audio Audio_Init(void)
{
    Audio audio = { 0 };
    audio.spec.freq = 44100;
    audio.spec.format = AUDIO_S16SYS;
    audio.spec.channels = 2;
    audio.spec.samples = 1024;
    audio.spec.callback = NULL;
    audio.dev = SDL_OpenAudioDevice(NULL, 0, &audio.spec, NULL, 0);
    return audio;
}

static void Audio_Free(Audio* audio)
{
    SDL_CloseAudioDevice(audio->dev);
}

static int32_t Audio_Play(void* data)
{
    const float amplification = 4;
    AudioConsumer* consumer = data;
    for(int32_t cycles = 0; consumer->audio->done == false; cycles++)
    {
        const uint32_t queue_size = SDL_GetQueuedAudioSize(consumer->audio->dev);
        const uint32_t samples = consumer->audio->spec.samples;
        const uint32_t thresh_min = 3 * consumer->audio->spec.samples;
        const uint32_t thresh_max = 5 * consumer->audio->spec.samples;
        SDL_PauseAudioDevice(consumer->audio->dev, queue_size < thresh_min);
        if(queue_size < thresh_max)
        {
            const uint32_t mixes_size = sizeof(int16_t) * samples;
            int16_t* const mixes = malloc(mixes_size);
            for(uint32_t sample = 0; sample < samples; sample += consumer->audio->spec.channels)
            {
                int16_t mix = 0;
                for(uint32_t note_index = 0; note_index < CONST_NOTES_MAX; note_index++)
                {
                    for(uint8_t channel = 0; channel < CONST_CHANNEL_MAX; channel++)
                    {
                        Note* note = &consumer->notes->note[note_index][channel];
                        if(note->on)
                        {
                            Note_Roll(note);
                            Note_Clamp(note);
                            const bool audible = note->gain > 0;
                            if(audible)
                            {
                                const uint8_t instrument = consumer->meta->instruments[channel];
                                Wave wave = { note, consumer->meta, channel, note_index, consumer->audio->spec.freq };
                                mix += WAVE_WAVEFORMS[instrument](&wave);
                            }
                        }
                    }
                }
                mix *= amplification;
                for(uint32_t speaker = 0; speaker < consumer->audio->spec.channels; speaker++)
                    mixes[sample + speaker] = mix;
            }
            SDL_LockAudioDevice(consumer->audio->dev);
            SDL_QueueAudio(consumer->audio->dev, mixes, mixes_size);
            SDL_UnlockAudioDevice(consumer->audio->dev);
            free(mixes);
        }
        if(cycles % 10 == 0)
            Notes_Draw(consumer->notes, consumer->meta);
        SDL_Delay(1);
    }
    return 0;
}

static Midi Midi_Init(Bytes* bytes)
{
    Midi midi = { 0 };
    midi.id = Bytes_U32(bytes, 0);
    midi.size = Bytes_U32(bytes, 4);
    midi.format_type = Bytes_U16(bytes, 8);
    midi.track_count = Bytes_U16(bytes, 10);
    midi.time_division = Bytes_U16(bytes, 12);
    midi.track = calloc(midi.track_count, sizeof(*midi.track));
    uint32_t offset = 14;
    for(uint32_t number = 0; number < midi.track_count; number++)
    {
        if(number > 0)
        {
            offset += 8;
            offset += midi.track[number - 1].size;
        }
        midi.track[number] = Track_Init(bytes, offset, number);
    }
    return midi;
}

static void Midi_Free(Midi* midi)
{
    for(uint32_t number = 0; number < midi->track_count; number++)
        Track_Free(&midi->track[number]);
    free(midi->track);
    midi->track = NULL;
}

static bool Midi_Done(Midi* midi)
{
    uint32_t count = 0;
    for(uint32_t i = 0; i < midi->track_count; i++)
        if(midi->track[i].run)
            count += 1;
    return count == 0;
}

static uint32_t Midi_ShaveTicks(Midi* midi)
{
    uint32_t ticks = UINT32_MAX;
    for(uint32_t i = 0; i < midi->track_count; i++)
    {
        Track* track = &midi->track[i];
        if(track->run)
            if(track->delay < ticks)
                ticks = track->delay;
    }
    for(uint32_t i = 0; i < midi->track_count; i++)
    {
        Track* track = &midi->track[i];
        if(track->run)
            track->delay -= ticks - 1;
    }
    return ticks;
}

static uint32_t Midi_ToMicrosecondDelay(Midi* midi, Meta* meta)
{
    const bool use_ticks = (midi->time_division & 0x8000) == 0;
    if(use_ticks)
    {
        const uint32_t ticks = Midi_ShaveTicks(midi);
        const uint32_t microseconds = ticks * meta->tempo / midi->time_division;
        return microseconds;
    }
    else // FRAMES PER SECOND.
    {
        fprintf(stderr, "FRAMES PER SECOND DELAYS ARE CURRENTLY NOT SUPPORTED\n");
        exit(1);
    }
}

static void Midi_Play(Midi* midi, Notes* notes, Meta* meta)
{
    for(;;)
    {
        for(uint32_t i = 0; i < midi->track_count; i++)
            Track_Play(&midi->track[i], notes, meta);
        if(Midi_Done(midi))
            break;
        const uint32_t microseconds = Midi_ToMicrosecondDelay(midi, meta);
        const uint32_t milliseconds = roundf(microseconds / 1000.0f);
        SDL_Delay(milliseconds);
    }
}

int main(const int argc, char** argv)
{
    SDL_Init(SDL_INIT_AUDIO);
    Args args = Args_Init(argc, argv);
    Audio audio = Audio_Init();
    Bytes bytes = Bytes_FromFile(args.file);
    Notes notes = Notes_Init();
    Meta meta = { 0 };
    AudioConsumer consumer = { &audio, &notes, &meta };
    SDL_Thread* thread = SDL_CreateThread(Audio_Play, "MIDI CONSUMER", &consumer);
    do // PRODUCE.
    {
        Midi midi = Midi_Init(&bytes);
        Midi_Play(&midi, &notes, &meta);
        Midi_Free(&midi);
    }
    while(args.loop);
    audio.done = true;
    SDL_WaitThread(thread, NULL);
    Bytes_Free(&bytes);
    Args_Free(&args);
    Audio_Free(&audio);
    Notes_Free(&notes);
    SDL_Quit();
    exit(ERROR_NONE);
}
