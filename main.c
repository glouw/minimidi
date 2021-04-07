#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

static bool DONE = false;

typedef enum
{
    CONST_NOTE_ATTACK = 4,
    CONST_NOTE_AMPLIFICATION = 6,
    CONST_NOTES_MAX = 128,
    CONST_CHANNEL_MAX = 16,
    CONST_NOTE_DECAY = 240,
    CONST_BEND_DEFAULT = 8192,
    CONST_SAMPLE_FREQ = 44100,
    CONST_XRES = 1600,
    CONST_YRES = 1000,
    CONST_VIDEO_SAMPLES = 2048,
}
Const;

enum
{
    ERROR_NONE,
    ERROR_ARGC,
    ERROR_FILE,
    ERROR_CRASH,
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
    int bend_last;
    int cycle;
    float id;
    bool on;
    bool wait;
    bool was_init;
}
Note;

typedef struct
{
    Note note[CONST_CHANNEL_MAX][CONST_NOTES_MAX];
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
}
Audio;

typedef struct
{
    SDL_Window* window;
    SDL_Renderer* renderer;
}
Video;

typedef struct
{
    Audio* audio;
    Notes* notes;
    Meta* meta;
    Video* video;
}
Consumer;

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

static Bytes
Bytes_FromFile(FILE* file)
{
    fseek(file, 0, SEEK_END);
    uint32_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    Bytes bytes = { 0 };
    bytes.size = size;
    bytes.data = calloc(bytes.size, sizeof(*bytes.data));
    for(uint32_t i = 0; i < bytes.size; i++)
        bytes.data[i] = getc(file);
    return bytes;
}

static void
Bytes_Free(Bytes* bytes)
{
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}

static uint8_t
Bytes_U8(Bytes* bytes, uint32_t index)
{
    return bytes->data[index];
}

static uint16_t
Bytes_U16(Bytes* bytes, uint32_t index)
{
    return Bytes_U8(bytes, index + 0) << 8 | Bytes_U8(bytes, index + 1);
}

static uint32_t
Bytes_U32(Bytes* bytes, uint32_t index)
{
    return Bytes_U16(bytes, index + 0) << 16 | Bytes_U16(bytes, index + 2);
}

static bool
Note_IsEvenCycle(Note* note)
{
    return (note->cycle % 2) == 0;
}

static void
Note_Clamp(Note* note)
{
    int min = 0;
    int max = CONST_NOTE_ATTACK * 127;
    if(note->gain < min) note->gain = min;
    if(note->gain > max) note->gain = max;
}

static void
Note_Roll(Note* note)
{
    int diff = note->gain_setpoint - note->gain;
    int decay = CONST_NOTE_DECAY;
    if(diff == 0)
    {
        if(note->gain == 0)
        {
            note->was_init = false;
            note->on = false;
        }
        // Note decays when held.
        else
        {
            if(note->progress != 0)
            {
                bool must_decay = note->progress % decay == 0;
                if(must_decay)
                {
                    note->gain -= 1;
                    note->gain_setpoint -= 1;
                }
            }
        }
    }
    // Note delta ramp - prevents clicks and pops.
    else
    {
        int step = diff / abs(diff);
        note->gain += step;
    }
}

static Notes
Notes_Init(void)
{
    Notes notes = { 0 };
    notes.display_rows = 32;
    notes.display_cols = CONST_NOTES_MAX / notes.display_rows;
    return notes;
}

static void
Notes_Free(Notes* notes)
{
    // Patch up display from clipping with prompt at exit.
    for(uint32_t i = 0; i < notes->display_rows; i++)
        putchar('\n');
}

static void
Notes_Draw(Notes* notes, Meta* meta)
{
    char* grn = "\x1b[0;32m";
    char* red = "\x1b[0;31m";
    char* nrm = "\x1b[0;00m";
    int note_index = 0;
    int draw_per_channel = 4;
    for(uint32_t y = 0; y < notes->display_rows; y++)
    {
        for(uint32_t x = 0; x < notes->display_cols; x++)
        {
            bool audible = false;
            for(uint8_t channel = 0; channel < CONST_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[channel][note_index];
                audible = note->gain > 0;
                if(audible)
                    break;
            }
            printf("%s%4d", audible ? grn : red, note_index);
            int got = 0;
            for(uint8_t channel = 0; channel < CONST_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[channel][note_index];
                bool draw = note->gain > 0;
                if(draw)
                {
                    uint8_t instrument = meta->instruments[channel];
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
    // Go up by number of display rows and leftmost column.
    printf("\x1B[%dA", notes->display_rows);
    printf("\r");
    printf(nrm);
}

static float
Note_Freq(Note* note)
{
    return 440.0f * powf(2.0f, (note->id - 69.0f) / 12.0f);
}

static float
Wave_Step(Wave* wave, float progress)
{
    float freq = Note_Freq(wave->note);
    float pi = 3.14159265358979323846f;
    return (progress * (2.0f * pi) * freq) / CONST_SAMPLE_FREQ;
}

static float
Wave_Tick(Wave* wave)
{
    int bend = wave->meta->bend[wave->channel];
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
    float x0 = Wave_Step(wave, wave->note->progress - 0.2f);
    float x1 = Wave_Step(wave, wave->note->progress + 0.0f);
    float a = wave->note->gain * sinf(x0);
    float b = wave->note->gain * sinf(x1);
    bool crossed = a < 0.0f && b > 0.0f;
    if(crossed)
    {
        wave->note->cycle += 1;
        // Note frequency can only be changed at axis crossing.
        if(wave->note->wait)
        {
            float bend_semitones = 12.0f;
            float bend_id = (bend - CONST_BEND_DEFAULT) / (CONST_BEND_DEFAULT / bend_semitones);
            wave->note->id = bend_id + wave->id;
            wave->note->wait = false;
            wave->note->progress = 0;
        }
    }
    float x = Wave_Step(wave, wave->note->progress);
    wave->note->progress += 1;
    return x;
}

static int16_t // [1]
Wave_Sin(Wave* wave)
{
    float x = Wave_Tick(wave);
    return wave->note->gain * sinf(x);
}

static int16_t // [2]
Wave_SinHalf(Wave* wave)
{
    int16_t amp = Wave_Sin(wave);
    return amp > 0 ? (0.8f * amp) : 0;
}

static int16_t // [3]
Wave_SinAbs(Wave* wave)
{
    int16_t amp = Wave_Sin(wave);
    return abs(amp);
}

static int16_t // [4]
Wave_SinQuarter(Wave* wave)
{
    float f = Wave_Step(wave, wave->note->progress);
    int16_t x = 0.4f * Wave_SinHalf(wave);
    return cosf(f) > 0.0f ? x : 0;
}

static int16_t // [5]
Wave_SinAlt(Wave* wave)
{
    int x = Wave_Sin(wave);
    return Note_IsEvenCycle(wave->note) ? x : 0;
}

static int16_t // [6]
Wave_SinAbsAlt(Wave* wave)
{
    int x = Wave_SinAbs(wave);
    return Note_IsEvenCycle(wave->note) ? x : 0;
}

static int16_t // [7]
Wave_Square(Wave* wave)
{
    int16_t amp = Wave_Sin(wave);
    return (amp >= 0 ? wave->note->gain : -wave->note->gain) / 10.0f;
}

static int16_t // [8] NOTE: Replaces sawtooth
Wave_Triangle(Wave* wave)
{
    float x = Wave_Tick(wave);
    return wave->note->gain * asinf(sinf(x)) / 1.5708f / 3.0f;
}

static int16_t // [9]
Wave_TriangleHalf(Wave* wave)
{
    int16_t amp = Wave_Triangle(wave);
    return amp > 0 ? (2.5f * amp) : 0;
}

static int16_t
(*WAVE_WAVEFORMS[])(Wave* wave) = {
    // Piano.
    [   0 ] = Wave_Triangle,
    [   1 ] = Wave_Triangle,
    [   2 ] = Wave_Triangle,
    [   3 ] = Wave_Triangle,
    [   4 ] = Wave_Triangle,
    [   5 ] = Wave_Triangle,
    [   6 ] = Wave_Triangle,
    [   7 ] = Wave_Triangle,
    // Chromatic Percussion.
    [   8 ] = Wave_Square,
    [   9 ] = Wave_Sin,
    [  10 ] = Wave_Sin,
    [  11 ] = Wave_Sin,
    [  12 ] = Wave_SinHalf,
    [  13 ] = Wave_Sin,
    [  14 ] = Wave_Sin,
    [  15 ] = Wave_Sin,
    // Organ.
    [  16 ] = Wave_Square,
    [  17 ] = Wave_Square,
    [  18 ] = Wave_Square,
    [  19 ] = Wave_Square,
    [  20 ] = Wave_Square,
    [  21 ] = Wave_Square,
    [  22 ] = Wave_Square,
    [  23 ] = Wave_Square,
    // Guitar.
    [  24 ] = Wave_SinQuarter,
    [  25 ] = Wave_SinHalf,
    [  26 ] = Wave_Square,
    [  27 ] = Wave_TriangleHalf,
    [  28 ] = Wave_SinHalf,
    [  29 ] = Wave_SinHalf,
    [  30 ] = Wave_SinHalf,
    [  31 ] = Wave_SinHalf,
    // Bass.
    [  32 ] = Wave_TriangleHalf,
    [  33 ] = Wave_SinHalf,
    [  34 ] = Wave_SinHalf,
    [  35 ] = Wave_SinHalf,
    [  36 ] = Wave_SinHalf,
    [  37 ] = Wave_SinHalf,
    [  38 ] = Wave_SinHalf,
    [  39 ] = Wave_TriangleHalf,
    // Strings.
    [  40 ] = Wave_Triangle,
    [  41 ] = Wave_Triangle,
    [  42 ] = Wave_Triangle,
    [  43 ] = Wave_Triangle,
    [  44 ] = Wave_Triangle,
    [  45 ] = Wave_Triangle,
    [  46 ] = Wave_Square,
    [  47 ] = Wave_SinHalf,
    // Strings (more).
    [  48 ] = Wave_Triangle,
    [  49 ] = Wave_Triangle,
    [  50 ] = Wave_TriangleHalf,
    [  51 ] = Wave_Triangle,
    [  52 ] = Wave_Triangle,
    [  53 ] = Wave_Triangle,
    [  54 ] = Wave_Triangle,
    [  55 ] = Wave_Triangle,
    // Brass.
    [  56 ] = Wave_TriangleHalf,
    [  57 ] = Wave_TriangleHalf,
    [  58 ] = Wave_TriangleHalf,
    [  59 ] = Wave_TriangleHalf,
    [  60 ] = Wave_Triangle,
    [  61 ] = Wave_TriangleHalf,
    [  62 ] = Wave_TriangleHalf,
    [  63 ] = Wave_TriangleHalf,
    // Reed.
    [  64 ] = Wave_Square,
    [  65 ] = Wave_Square,
    [  66 ] = Wave_Square,
    [  67 ] = Wave_Square,
    [  68 ] = Wave_Square,
    [  69 ] = Wave_Square,
    [  70 ] = Wave_Square,
    [  71 ] = Wave_Square,
    // Pipe.
    [  72 ] = Wave_Sin,
    [  73 ] = Wave_Sin,
    [  74 ] = Wave_Sin,
    [  75 ] = Wave_Sin,
    [  76 ] = Wave_Sin,
    [  77 ] = Wave_Sin,
    [  78 ] = Wave_Sin,
    [  79 ] = Wave_Triangle,
    // Synth Lead.
    [  80 ] = Wave_Sin,
    [  81 ] = Wave_Triangle,
    [  82 ] = Wave_Triangle,
    [  83 ] = Wave_Sin,
    [  84 ] = Wave_Sin,
    [  85 ] = Wave_Sin,
    [  86 ] = Wave_Sin,
    [  87 ] = Wave_Sin,
    // Synth Pad.
    [  88 ] = Wave_Sin,
    [  89 ] = Wave_Sin,
    [  90 ] = Wave_Sin,
    [  91 ] = Wave_Sin,
    [  92 ] = Wave_Sin,
    [  93 ] = Wave_Sin,
    [  94 ] = Wave_Sin,
    [  95 ] = Wave_Sin,
    // Synth Effects.
    [  96 ] = Wave_Sin,
    [  97 ] = Wave_Sin,
    [  98 ] = Wave_Sin,
    [  99 ] = Wave_Sin,
    [ 100 ] = Wave_Sin,
    [ 101 ] = Wave_Sin,
    [ 102 ] = Wave_Sin,
    [ 103 ] = Wave_Sin,
    // Ethnic.
    [ 104 ] = Wave_Sin,
    [ 105 ] = Wave_Sin,
    [ 106 ] = Wave_Sin,
    [ 107 ] = Wave_Sin,
    [ 108 ] = Wave_Sin,
    [ 109 ] = Wave_Sin,
    [ 110 ] = Wave_Sin,
    [ 111 ] = Wave_Sin,
    // Percussive.
    [ 112 ] = Wave_Sin,
    [ 113 ] = Wave_Sin,
    [ 114 ] = Wave_Sin,
    [ 115 ] = Wave_Sin,
    [ 116 ] = Wave_Sin,
    [ 117 ] = Wave_Sin,
    [ 118 ] = Wave_Sin,
    [ 119 ] = Wave_Sin,
    // Sound Effects.
    [ 120 ] = Wave_Sin,
    [ 121 ] = Wave_Sin,
    [ 122 ] = Wave_Sin,
    [ 123 ] = Wave_Sin,
    [ 124 ] = Wave_Sin,
    [ 125 ] = Wave_Sin,
    [ 126 ] = Wave_Sin,
    [ 127 ] = Wave_Sin,
};

static Args
Args_Init(int argc, char** argv)
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

static void
Args_Free(Args* args)
{
    fclose(args->file);
}

static void
Track_Free(Track* track)
{
    free(track->data);
    track->data = NULL;
}

static void
Track_Back(Track* track)
{
    track->index -= 1;
}

static void
Track_Next(Track* track)
{
    track->index += 1;
}

static uint8_t
Track_U8(Track* track)
{
    uint8_t byte = track->data[track->index];
    Track_Next(track);
    return byte;
}

static uint32_t
Track_Var(Track* track)
{
    uint32_t var = 0x0;
    bool run = true;
    while(run)
    {
        uint8_t byte = Track_U8(track);
        var = (var << 7) | (byte & 0x7F);
        run = byte >> 7;
    }
    return var;
}

static char*
Track_Str(Track* track)
{
    uint32_t len = Track_U8(track);
    char* str = calloc(len + 1, sizeof(*str));
    for(uint32_t i = 0; i < len; i++)
        str[i] = Track_U8(track);
    return str;
}

static Bytes
Track_Bytes(Track* track)
{
    Bytes bytes = { 0 };
    bytes.size = Track_Var(track);
    bytes.data = calloc(bytes.size, sizeof(*bytes.data));
    for(uint32_t i = 0; i < bytes.size; i++)
        bytes.data[i] = Track_U8(track);
    return bytes;
}

static void
Track_Crash(Track* track)
{
    int window = 30;
    for(int32_t i = -window; i < window; i++)
    {
        uint32_t j = track->index + i;
        if(j < track->size)
        {
            char* star = (j == track->index) ? "*" : "";
            fprintf(stderr, "index %d : 0x%02X%s\n", j, track->data[j], star);
        }
    }
    exit(ERROR_CRASH);
}

static uint8_t
Track_Status(Track* track, uint8_t status)
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

static bool
Notes_IsPercussive(uint8_t channel)
{
    return channel == 9;
}

static void
Track_RealEvent(Track* track, Meta* meta, Notes* notes, uint8_t leader)
{
    uint8_t channel = leader & 0xF;
    uint8_t status = leader >> 4;
    switch(Track_Status(track, status))
    {
        default:
        {
            Track_Crash(track);
            break;
        }
        // Note Off.
        case 0x8:
        {
            uint8_t note_index = Track_U8(track);
            Track_U8(track);
            if(!Notes_IsPercussive(channel))
            {
                Note* note = &notes->note[channel][note_index];
                note->gain_setpoint = 0;
                meta->bend[channel] = CONST_BEND_DEFAULT;
            }
            break;
        }
        // Note On.
        case 0x9:
        {
            uint8_t note_index = Track_U8(track);
            uint8_t note_velocity = Track_U8(track);
            if(!Notes_IsPercussive(channel))
            {
                Note* note = &notes->note[channel][note_index];
                note->gain_setpoint = CONST_NOTE_ATTACK * note_velocity;
                note->on = true;
                meta->bend[channel] = CONST_BEND_DEFAULT;
            }
            break;
        }
        // Note Aftertouch.
        case 0xA:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        // Controller.
        case 0xB:
        {
            uint8_t type = Track_U8(track);
            uint8_t value = Track_U8(track);
            switch(type)
            {
                // Channel Volume.
                case 0x07:
                {
                    for(uint32_t note_index = 0; note_index < CONST_NOTES_MAX; note_index++)
                    {
                        Note* note = &notes->note[channel][note_index];
                        bool audible = note->gain_setpoint > 0;
                        if(audible)
                            note->gain_setpoint = CONST_NOTE_ATTACK * value;
                    }
                    break;
                }
            }
            break;
        }
        // Program Change.
        case 0xC:
        {
            uint8_t program = Track_U8(track);
            meta->instruments[channel] = program;
            break;
        }
        // Channel Aftertouch.
        case 0xD:
        {
            Track_U8(track);
            break;
        }
        // Pitch Bend.
        case 0xE:
        {
            uint8_t lsb = Track_U8(track);
            uint8_t msb = Track_U8(track);
            uint16_t bend = (msb << 7) | lsb;
            meta->bend[channel] = bend;
            break;
        }
    }
}

static void
Track_MetaEvent(Track* track, Meta* meta)
{
    switch(Track_U8(track))
    {
        default:
        {
            Track_Crash(track);
            break;
        }
        // Sequence Number.
        case 0x00:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x01: // Text Event.
        case 0x02: // Copyright Notice.
        case 0x03: // Track Name.
        case 0x04: // Instrument Name.
        case 0x05: // Lyric.
        case 0x06: // Marker.
        case 0x07: // Cue Point.
        case 0x08: // Program Name.
        case 0x09: // Device Name.
        {
            free(Track_Str(track));
            break;
        }
        // Channel Prefix.
        case 0x20:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        // Midi Port.
        case 0x21:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        // End of Track.
        case 0x2F:
        {
            track->run = Track_U8(track);
            break;
        }
        // Tempo.
        case 0x51:
        {
            Track_U8(track);
            uint8_t a = Track_U8(track);
            uint8_t b = Track_U8(track);
            uint8_t c = Track_U8(track);
            meta->tempo = (a << 16) | (b << 8) | c;
            break;
        }
        // SMPTE Offset.
        case 0X54:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        // Time Signature.
        case 0x58:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        // Key Signature.
        case 0x59:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0xF0: // Sysex Start.
        case 0xF7: // Sysex End.
        case 0x7F: // Systex Running Status.
        {
            Bytes bytes = Track_Bytes(track);
            Bytes_Free(&bytes);
            break;
        }
    }
}

static void
Track_Play(Track* track, Notes* notes, Meta* meta)
{
    int end = -1;
    if(track->run)
    {
        if(track->delay > end)
            track->delay -= 1;
        if(track->delay == end)
            track->delay = Track_Var(track);
        if(track->delay == 0)
        {
            uint8_t leader = Track_U8(track);
            leader == 0xFF
                ? Track_MetaEvent(track, meta)
                : Track_RealEvent(track, meta, notes, leader);
            // Notes with zero delay must immediately process
            // the next note before moving onto the next track.
            Track_Play(track, notes, meta);
        }
    }
}

static Track
Track_Init(Bytes* bytes, uint32_t offset, uint32_t number)
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

static Audio
Audio_Init(void)
{
    Audio audio = { 0 };
    audio.spec.freq = CONST_SAMPLE_FREQ;
    audio.spec.format = AUDIO_S16SYS;
    audio.spec.channels = 2;
    audio.spec.samples = 1024;
    audio.spec.callback = NULL;
    audio.dev = SDL_OpenAudioDevice(NULL, 0, &audio.spec, NULL, 0);
    return audio;
}

static void
Audio_Free(Audio* audio)
{
    SDL_CloseAudioDevice(audio->dev);
}

static int
Audio_Play(void* data)
{
    Consumer* consumer = data;
    for(int32_t cycles = 0; !DONE; cycles++)
    {
        uint32_t queue_size = SDL_GetQueuedAudioSize(consumer->audio->dev);
        uint32_t samples = consumer->audio->spec.samples;
        uint32_t thresh_min = 3 * consumer->audio->spec.samples;
        uint32_t thresh_max = 5 * consumer->audio->spec.samples;
        SDL_PauseAudioDevice(consumer->audio->dev, queue_size < thresh_min);
        if(queue_size < thresh_max)
        {
            uint32_t mixes_size = sizeof(int16_t) * samples;
            int16_t* mixes = malloc(mixes_size);
            for(uint32_t sample = 0; sample < samples; sample += consumer->audio->spec.channels)
            {
                int16_t mix = 0;
                for(uint32_t note_index = 0; note_index < CONST_NOTES_MAX; note_index++)
                {
                    for(uint8_t channel = 0; channel < CONST_CHANNEL_MAX; channel++)
                    {
                        Note* note = &consumer->notes->note[channel][note_index];
                        if(note->on)
                        {
                            Note_Roll(note);
                            Note_Clamp(note);
                            bool audible = note->gain > 0;
                            if(audible)
                            {
                                uint8_t instrument = consumer->meta->instruments[channel];
                                Wave wave = { note, consumer->meta, channel, note_index };
                                int16_t sample = WAVE_WAVEFORMS[instrument](&wave);
                                mix += sample;
                            }
                        }
                    }
                }
                mix *= CONST_NOTE_AMPLIFICATION;
                for(uint32_t speaker = 0; speaker < consumer->audio->spec.channels; speaker++)
                    mixes[sample + speaker] = mix;
            }
            SDL_LockAudioDevice(consumer->audio->dev);
            SDL_QueueAudio(consumer->audio->dev, mixes, mixes_size);
            SDL_UnlockAudioDevice(consumer->audio->dev);
            free(mixes);
        }
        SDL_Delay(1);
    }
    return 0;
}

static Midi
Midi_Init(Bytes* bytes)
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

static void
Midi_Free(Midi* midi)
{
    for(uint32_t number = 0; number < midi->track_count; number++)
        Track_Free(&midi->track[number]);
    free(midi->track);
    midi->track = NULL;
}

static bool
Midi_Done(Midi* midi)
{
    uint32_t count = 0;
    for(uint32_t i = 0; i < midi->track_count; i++)
        if(midi->track[i].run)
            count += 1;
    return count == 0;
}

static uint32_t
Midi_ShaveTicks(Midi* midi)
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

static uint32_t
Midi_ToMicrosecondDelay(Midi* midi, Meta* meta)
{
    bool use_ticks = (midi->time_division & 0x8000) == 0;
    if(use_ticks)
    {
        uint32_t ticks = Midi_ShaveTicks(midi);
        uint32_t microseconds = ticks * meta->tempo / midi->time_division;
        return microseconds;
    }
    // Frames Per Second.
    else
    {
        fprintf(stderr, "FRAMES PER SECOND DELAYS ARE CURRENTLY NOT SUPPORTED\n");
        exit(1);
    }
}

static void
Midi_Play(Midi* midi, Notes* notes, Meta* meta)
{
    while(!DONE)
    {
        for(uint32_t i = 0; i < midi->track_count; i++)
            Track_Play(&midi->track[i], notes, meta);
        uint32_t microseconds = Midi_ToMicrosecondDelay(midi, meta);
        uint32_t milliseconds = roundf(microseconds / 1000.0f);
        SDL_Delay(milliseconds);
        if(Midi_Done(midi))
            DONE = true;
    }
}

Video
Video_Init(void)
{
    Video video;
    SDL_CreateWindowAndRenderer(CONST_XRES, CONST_YRES, 0, &video.window, &video.renderer);
    return video;
}

void
Video_Free(Video* video)
{
    SDL_DestroyRenderer(video->renderer);
    SDL_DestroyWindow(video->window);
}

void
Video_VisualizeWaveforms(Video* video)
{
    int16_t (*waveforms[])(Wave* wave) = {
        Wave_Sin,
        Wave_SinHalf,
        Wave_SinAbs,
        Wave_SinQuarter,
        Wave_SinAlt,
        Wave_SinAbsAlt,
        Wave_Square,
        Wave_Triangle,
        Wave_TriangleHalf,
    };
    SDL_SetRenderDrawColor(video->renderer, 0x00, 0x00, 0x00, 0x00);
    SDL_RenderClear(video->renderer);
    SDL_SetRenderDrawColor(video->renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    int len = sizeof(waveforms) / sizeof(*waveforms);
    for(int j = 0; j < len; j++)
    {
        Meta meta = { 0 };
        Note note = { .on = true, .gain = 1024 };
        Wave wave = { &note, &meta, 0, 0 };
        float freq = Note_Freq(&note);
        int samples = 6 * CONST_SAMPLE_FREQ / freq;
        int yy = CONST_YRES / (2 * len);
        int xx = CONST_XRES;
        int offset = -yy * (2 * j + 1);
        int x0 = 0;
        int y0 = 0;
        for(int i = 0; i < samples; i++)
        {
            int x = +xx * i / (float) samples;
            int y = -yy * waveforms[j](&wave) / (float) note.gain - offset;
            if(i % 32 == 0)
            {
                SDL_RenderDrawLine(video->renderer, x0, y0, x, y);
                x0 = x;
                y0 = y;
            }
        }
    }
    SDL_RenderPresent(video->renderer);
}

void
Video_Draw(Video* video, Meta* meta, Notes* notes)
{
    uint32_t colors[CONST_CHANNEL_MAX] = {
        0x414b7e, 0x636fb2, 0xadc4ff, 0xffffff, 0xffccd7, 0xff7fbd, 0x872450, 0xe52d40,
        0xef604a, 0xffd877, 0x00cc8b, 0x005a75, 0x513ae8, 0x19baff, 0x7731a5, 0xb97cff,
    };
    SDL_SetRenderDrawColor(video->renderer, 0x00, 0x00, 0x00, 0x00);
    SDL_RenderClear(video->renderer);
    for(int channel = 0; channel < CONST_CHANNEL_MAX; channel++)
    {
        uint32_t color = colors[channel];
        uint8_t r = color >> 0x10;
        uint8_t g = color >> 0x08;
        uint8_t b = color >> 0x00;
        SDL_SetRenderDrawColor(video->renderer, r, g, b, 0xFF);
        uint8_t instrument = meta->instruments[channel];
        float* buffer = calloc(CONST_VIDEO_SAMPLES, sizeof(*buffer));
        for(int note_index = 0; note_index < CONST_NOTES_MAX; note_index++)
        {
            Note copy = notes->note[channel][note_index];
            if(copy.on)
            {
                copy.progress = 0;
                Wave wave = { &copy, meta, channel, note_index };
                for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
                    buffer[i] += WAVE_WAVEFORMS[instrument](&wave);
            }
        }
        float max = 0;
        for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
            if(buffer[i] > max)
                max = buffer[i];
        for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
            buffer[i] /= max;
        {
            for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
            {
                int h = CONST_YRES / CONST_CHANNEL_MAX;
                int a = h / 2;
                int x = CONST_XRES * (i / (float) CONST_VIDEO_SAMPLES);
                int y = channel * h + -a * (buffer[i] - 1.0f);
                SDL_RenderDrawPoint(video->renderer, x, y);
            }
        }
        free(buffer);
    }
    SDL_RenderPresent(video->renderer);
}

int
Video_Play(void* data)
{
    Consumer* consumer = data;
    for(int32_t cycles = 0; !DONE; cycles++)
    {
        SDL_Event e;
        SDL_PollEvent(&e);
        if(e.type == SDL_QUIT)
            DONE = true;
        if(cycles % 10 == 0)
        {
            Video_Draw(consumer->video, consumer->meta, consumer->notes);
            Notes_Draw(consumer->notes, consumer->meta);
        }
        SDL_Delay(1);
    }
    return 0;
}

int
main(int argc, char** argv)
{
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
    Args args = Args_Init(argc, argv);
    Video video = Video_Init();
    Audio audio = Audio_Init();
    Bytes bytes = Bytes_FromFile(args.file);
    Notes notes = Notes_Init();
    Meta meta = { 0 };
    // Consume...
    Consumer consumer = { &audio, &notes, &meta, &video };
    SDL_Thread* audio_thread = SDL_CreateThread(Audio_Play, "MIDI-AUDIO-CONSUMER", &consumer);
    SDL_Thread* video_thread = SDL_CreateThread(Video_Play, "MIDI-VIDEO-CONSUMER", &consumer);
    // .. And produce.
    do
    {
        Midi midi = Midi_Init(&bytes);
        Midi_Play(&midi, &notes, &meta);
        Midi_Free(&midi);
    }
    while(args.loop);
    SDL_WaitThread(audio_thread, NULL);
    SDL_WaitThread(video_thread, NULL);
    Video_Free(&video);
    Bytes_Free(&bytes);
    Args_Free(&args);
    Audio_Free(&audio);
    Notes_Free(&notes);
    SDL_Quit();
    exit(ERROR_NONE);
}
