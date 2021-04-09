// TODO:
// 1. Implement proper attack ADSR:
// (A) ATTACK
// (D) DECAY
// (S) SUSTAIN
// (R) RELEASE
//
/*       /\                   */
/*      /  \                  */
/*     /    \                 */
/*    /      \__________      */
/*   /                  \     */
/*  /                    \    */
/* /                      \   */
/*/                        \  */
/* --A----D-------S-------R-- */

// 2.Build a signal viewer (needed for properly for viewing FM signals)

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
    CONST_NOTE_DECAY = 300,
    CONST_BEND_DEFAULT = 8192,
    CONST_SAMPLE_FREQ = 44100,
    CONST_XRES = 800,
    CONST_YRES = 600,
    CONST_VIDEO_SAMPLES = 2048,
    CONST_VIDEO_GRAIN = 5,
    CONST_MODULATION_GAIN = 512,
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
}
Notes;

typedef struct
{
    Note* modu;
    Meta* meta;
    uint8_t channel;
    int id;
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
    Notes* modus;
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
Note_Roll(Note* note) // Attack and Decay/Sustain.
{
    int diff = note->gain_setpoint - note->gain;
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
                bool must_decay = note->progress % CONST_NOTE_DECAY == 0;
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

static void
Note_Process(Note* note)
{
    Note_Roll(note);
    Note_Clamp(note);
}

static float
Note_Freq(Note* note)
{
    return 440.0f * powf(2.0f, (note->id - 69.0f) / 12.0f);
}

static float
Note_Step(Note* note, float progress)
{
    float freq = Note_Freq(note);
    float pi = 3.14159265358979323846f;
    return (progress * (2.0f * pi) * freq) / CONST_SAMPLE_FREQ;
}

static float
Note_Tick(Note* note, int bend, int id)
{
    if(!note->was_init)
    {
        note->was_init = true;
        note->id = id;
    }
    if(bend != note->bend_last)
    {
        note->bend_last = bend;
        note->wait = true;
    }
    float x0 = Note_Step(note, note->progress - 0.2f);
    float x1 = Note_Step(note, note->progress + 0.0f);
    float a = note->gain * sinf(x0);
    float b = note->gain * sinf(x1);
    bool crossed = a < 0.0f && b > 0.0f;
    if(crossed)
    {
        note->cycle += 1;
        // Note frequency can only be changed at axis crossing.
        if(note->wait)
        {
            float bend_semitones = 12.0f;
            float bend_id = (bend - CONST_BEND_DEFAULT) / (CONST_BEND_DEFAULT / bend_semitones);
            note->id = bend_id + id;
            note->wait = false;
            note->progress = 0;
        }
    }
    float x = Note_Step(note, note->progress);
    note->progress += 1;
    return x;
}

static void
Notes_Setup(Notes* modus)
{
    for(int i = 0; i < CONST_CHANNEL_MAX; i++)
    for(int j = 0; j < CONST_NOTES_MAX; j++)
    {
        Note* note = &modus->note[i][j];
        note->gain = note->gain_setpoint = CONST_MODULATION_GAIN;
    }
}

static int16_t // [1]
Wave_Sin
(Wave* wave, Note* note, float fm)
{
    int bend = wave->meta->bend[wave->channel];
    float x = Note_Tick(note, bend, wave->id);
    return note->gain * sinf(x + fm);
}

static int16_t // [2]
Wave_SinHalf
(Wave* wave, Note* note, float fm)
{
    int16_t amp = Wave_Sin(wave, note, fm);
    return amp > 0 ? (1.1f * amp) : 0;
}

static int16_t // [3]
Wave_SinAbs
(Wave* wave, Note* note, float fm)
{
    int16_t amp = Wave_Sin(wave, note, fm);
    return abs(amp);
}

static int16_t // [4]
Wave_SinQuarter
(Wave* wave, Note* note, float fm)
{
    float f = Note_Step(note, note->progress);
    int16_t x = 0.4f * Wave_SinHalf(wave, note, fm);
    return cosf(f) > 0.0f ? x : 0;
}

static int16_t // [5]
Wave_SinAlt
(Wave* wave, Note* note, float fm)
{
    int x = Wave_Sin(wave, note, fm);
    return Note_IsEvenCycle(note) ? x : 0;
}

static int16_t // [6]
Wave_SinAbsAlt
(Wave* wave, Note* note, float fm)
{
    int x = Wave_SinAbs(wave, note, fm);
    return Note_IsEvenCycle(note) ? x : 0;
}

static int16_t // [7]
Wave_Square
(Wave* wave, Note* note, float fm)
{
    int16_t amp = Wave_Sin(wave, note, fm);
    return (amp >= 0 ? note->gain : -note->gain) / 8.0f;
}

static int16_t // [8] NOTE: Replaces sawtooth
Wave_Triangle
(Wave* wave, Note* note, float fm)
{
    int bend = wave->meta->bend[wave->channel];
    float x = Note_Tick(note, bend, wave->id);
    return note->gain * asinf(sinf(x + fm)) / 1.5708f / 3.0f;
}

static int16_t // [9]
Wave_TriangleHalf
(Wave* wave, Note* note, float fm)
{
    int16_t amp = Wave_Triangle(wave, note, fm);
    return amp > 0 ? (1.6f * amp) : 0;
}

static float
Flatten(int16_t gain)
{
    return gain / (1.0f * CONST_MODULATION_GAIN);
}

static int16_t
Wave_FM
(
    Wave* wave,
    Note* note,
    int16_t a(Wave*, Note*, float),
    int16_t b(Wave*, Note*, float),
    float multiplier
)
{
    return a(wave, note, multiplier * Flatten(b(wave, wave->modu, 0.0f)));
}

static int16_t
(Wave_Piano)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.2f * Wave_FM(wave, note, Wave_TriangleHalf, Wave_Sin, 1.0f);
}

static int16_t
(Wave_Synth)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.4f * Wave_FM(wave, note, Wave_Triangle, Wave_Sin, 1.0f);
}

static int16_t
(Wave_Guitar)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.5f * Wave_FM(wave, note, Wave_SinQuarter, Wave_Sin, 1.0f);
}

static int16_t
(Wave_Bass)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.8f * Wave_FM(wave, note, Wave_SinHalf, Wave_Sin, 1.0f);
}

static int16_t
(Wave_Pipe)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.35f * Wave_FM(wave, note, Wave_Square, Wave_Triangle, 0.5f);
}

static int16_t
(Wave_Strings)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.35f * Wave_FM(wave, note, Wave_TriangleHalf, Wave_Square, 1.0f);
}

static int16_t
(Wave_Brass)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.4f * Wave_FM(wave, note, Wave_Square, Wave_Sin, 1.0f);
}

static int16_t
(Wave_Reed)
(Wave* wave, Note* note, float fm)
{
    (void) fm;
    return 0.4f * Wave_FM(wave, note, Wave_Triangle, Wave_Sin, 1.0f);
}

static int16_t
(*WAVE_WAVEFORMS[])(Wave* wave, Note* note, float fm) = {
    // Piano.
    [   0 ] = Wave_Piano,
    [   1 ] = Wave_Piano,
    [   2 ] = Wave_Piano,
    [   3 ] = Wave_Piano,
    [   4 ] = Wave_Piano,
    [   5 ] = Wave_Piano,
    [   6 ] = Wave_Piano,
    [   7 ] = Wave_Piano,
    // Chromatic Percussion.
    [   8 ] = Wave_Piano,
    [   9 ] = Wave_Piano,
    [  10 ] = Wave_Piano,
    [  11 ] = Wave_Piano,
    [  12 ] = Wave_Piano,
    [  13 ] = Wave_Piano,
    [  14 ] = Wave_Piano,
    [  15 ] = Wave_Piano,
    // Organ.
    [  16 ] = Wave_Piano,
    [  17 ] = Wave_Piano,
    [  18 ] = Wave_Piano,
    [  19 ] = Wave_Piano,
    [  20 ] = Wave_Piano,
    [  21 ] = Wave_Piano,
    [  22 ] = Wave_Piano,
    [  23 ] = Wave_Piano,
    // Guitar.
    [  24 ] = Wave_Guitar,
    [  25 ] = Wave_Guitar,
    [  26 ] = Wave_Guitar,
    [  27 ] = Wave_Guitar,
    [  28 ] = Wave_Guitar,
    [  29 ] = Wave_Guitar,
    [  30 ] = Wave_Guitar,
    [  31 ] = Wave_Guitar,
    // Bass.
    [  32 ] = Wave_Bass,
    [  33 ] = Wave_Bass,
    [  34 ] = Wave_Bass,
    [  35 ] = Wave_Bass,
    [  36 ] = Wave_Bass,
    [  37 ] = Wave_Bass,
    [  38 ] = Wave_Bass,
    [  39 ] = Wave_Bass,
    // Strings.
    [  40 ] = Wave_Strings,
    [  41 ] = Wave_Strings,
    [  42 ] = Wave_Strings,
    [  43 ] = Wave_Strings,
    [  44 ] = Wave_Strings,
    [  45 ] = Wave_Strings,
    [  46 ] = Wave_Strings,
    [  47 ] = Wave_Strings,
    // Strings (more).
    [  48 ] = Wave_Strings,
    [  49 ] = Wave_Strings,
    [  50 ] = Wave_Strings,
    [  51 ] = Wave_Strings,
    [  52 ] = Wave_Strings,
    [  53 ] = Wave_Strings,
    [  54 ] = Wave_Strings,
    [  55 ] = Wave_Strings,
    // Brass.
    [  56 ] = Wave_Brass,
    [  57 ] = Wave_Brass,
    [  58 ] = Wave_Brass,
    [  59 ] = Wave_Brass,
    [  60 ] = Wave_Brass,
    [  61 ] = Wave_Brass,
    [  62 ] = Wave_Brass,
    [  63 ] = Wave_Brass,
    // Reed.
    [  64 ] = Wave_Reed,
    [  65 ] = Wave_Reed,
    [  66 ] = Wave_Reed,
    [  67 ] = Wave_Reed,
    [  68 ] = Wave_Reed,
    [  69 ] = Wave_Reed,
    [  70 ] = Wave_Reed,
    [  71 ] = Wave_Reed,
    // Pipe.
    [  72 ] = Wave_Pipe,
    [  73 ] = Wave_Pipe,
    [  74 ] = Wave_Pipe,
    [  75 ] = Wave_Pipe,
    [  76 ] = Wave_Pipe,
    [  77 ] = Wave_Pipe,
    [  78 ] = Wave_Pipe,
    [  79 ] = Wave_Pipe,
    // Synth Lead.
    [  80 ] = Wave_Synth,
    [  81 ] = Wave_Synth,
    [  82 ] = Wave_Synth,
    [  83 ] = Wave_Synth,
    [  84 ] = Wave_Synth,
    [  85 ] = Wave_Synth,
    [  86 ] = Wave_Synth,
    [  87 ] = Wave_Synth,
    // Synth Pad.
    [  88 ] = Wave_Synth,
    [  89 ] = Wave_Synth,
    [  90 ] = Wave_Synth,
    [  91 ] = Wave_Synth,
    [  92 ] = Wave_Synth,
    [  93 ] = Wave_Synth,
    [  94 ] = Wave_Synth,
    [  95 ] = Wave_Synth,
    // Synth Effects.
    [  96 ] = Wave_Synth,
    [  97 ] = Wave_Synth,
    [  98 ] = Wave_Synth,
    [  99 ] = Wave_Synth,
    [ 100 ] = Wave_Synth,
    [ 101 ] = Wave_Synth,
    [ 102 ] = Wave_Synth,
    [ 103 ] = Wave_Synth,
    // Ethnic.
    [ 104 ] = Wave_Piano,
    [ 105 ] = Wave_Piano,
    [ 106 ] = Wave_Piano,
    [ 107 ] = Wave_Piano,
    [ 108 ] = Wave_Piano,
    [ 109 ] = Wave_Piano,
    [ 110 ] = Wave_Piano,
    [ 111 ] = Wave_Piano,
    // Percussive.
    [ 112 ] = Wave_Piano,
    [ 113 ] = Wave_Piano,
    [ 114 ] = Wave_Piano,
    [ 115 ] = Wave_Piano,
    [ 116 ] = Wave_Piano,
    [ 117 ] = Wave_Piano,
    [ 118 ] = Wave_Piano,
    [ 119 ] = Wave_Piano,
    // Sound Effects.
    [ 120 ] = Wave_Piano,
    [ 121 ] = Wave_Piano,
    [ 122 ] = Wave_Piano,
    [ 123 ] = Wave_Piano,
    [ 124 ] = Wave_Piano,
    [ 125 ] = Wave_Piano,
    [ 126 ] = Wave_Piano,
    [ 127 ] = Wave_Piano,
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
IsPercussive(uint8_t channel)
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
            if(!IsPercussive(channel))
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
            if(!IsPercussive(channel))
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
            printf("channel[%2d] = %d\n", channel, program);
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
                        Note* modu = &consumer->modus->note[channel][note_index];
                        if(note->on)
                        {
                            Note_Process(note);
                            Note_Process(modu);
                            bool audible = note->gain > 0;
                            if(audible)
                            {
                                uint8_t instrument = consumer->meta->instruments[channel];
                                Wave wave = { modu, consumer->meta, channel, note_index };
                                mix += WAVE_WAVEFORMS[instrument](&wave, note, 0.0f);
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
Video_Draw(Video* video, Meta* meta, Notes* notes, Notes* modus)
{
    uint32_t colors[CONST_CHANNEL_MAX] = {
        0x414b7e, 0x636fb2, 0xadc4ff, 0xffffff, 0xffccd7, 0xff7fbd, 0x872450, 0xe52d40,
        0xef604a, 0xffd877, 0x00cc8b, 0x005a75, 0x513ae8, 0x19baff, 0x7731a5, 0xb97cff,
    };
    int h = CONST_YRES / CONST_CHANNEL_MAX;
    int amp = h / 2;
    int point_count = CONST_VIDEO_SAMPLES / CONST_VIDEO_GRAIN;
    // Clear screen.
    {
        SDL_SetRenderDrawColor(video->renderer, 0x00, 0x00, 0x00, 0x00);
        SDL_RenderClear(video->renderer);
    }
    // Draw channel lines.
    {
        SDL_SetRenderDrawColor(video->renderer, 0xAA, 0xAA, 0xAA, 0xAA);
        for(int i = 0; i < CONST_CHANNEL_MAX; i++)
        {
            int y = i * h;
            SDL_RenderDrawLine(video->renderer, 0, y, CONST_XRES, y);
        }
    }
    for(int channel = 0; channel < CONST_CHANNEL_MAX; channel++)
    {
        // Buffer signal (zero phase).
        float buffer[CONST_VIDEO_SAMPLES] = { 0 };
        {
            uint8_t instrument = meta->instruments[channel];
            for(int note_index = 0; note_index < CONST_NOTES_MAX; note_index++)
            {
                Note note = notes->note[channel][note_index];
                Note modu = modus->note[channel][note_index];
                if(note.on)
                {
                    note.progress = modu.progress = 0;
                    Wave wave = { &modu, meta, channel, note_index };
                    for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
                        buffer[i] += WAVE_WAVEFORMS[instrument](&wave, &note, 0.0f);
                }
            }
        }
        // Scale signal.
        {
            float max = 0;
            for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
                if(buffer[i] > max)
                    max = buffer[i];
            for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
                buffer[i] /= max;
        }
        // Collect signal into an array of points.
        SDL_Point points[point_count];
        {
            int index = 0;
            for(int i = 0; i < CONST_VIDEO_SAMPLES; i++)
                if(i % CONST_VIDEO_GRAIN == 0)
                {
                    int x = CONST_XRES * (i / (float) CONST_VIDEO_SAMPLES);
                    int y = channel * h - amp * (buffer[i] - 1.0f);
                    points[index++] = (SDL_Point) { x, y };
                }
        }
        // Draw signal points.
        {
            uint32_t color = colors[channel];
            uint8_t r = color >> 0x10;
            uint8_t g = color >> 0x08;
            uint8_t b = color >> 0x00;
            SDL_SetRenderDrawColor(video->renderer, r, g, b, 0xFF);
            SDL_RenderDrawLines(video->renderer, points, point_count);
        }
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
        Video_Draw(consumer->video, consumer->meta, consumer->notes, consumer->modus);
        SDL_Delay(10);
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
    Notes notes = { 0 };
    Notes modus = { 0 };
    Meta meta = { 0 };
    Notes_Setup(&modus);
    // Consume...
    Consumer consumer = { &audio, &notes, &modus, &meta, &video };
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
    SDL_Quit();
    exit(ERROR_NONE);
}
