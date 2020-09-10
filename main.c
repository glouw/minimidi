#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <unistd.h>
#include <assert.h>

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
    uint8_t* data;
    uint32_t size;
}
Bytes;

typedef struct
{
    SDL_AudioSpec spec;
    SDL_AudioDeviceID dev;
}
Audio;

typedef struct
{
    SDL_atomic_t gain;
    SDL_atomic_t gain_setpoint;
    SDL_atomic_t progress;
    SDL_atomic_t channel;
}
Note;

typedef struct
{
    Note* note;
    uint32_t size;
    uint32_t display_rows;
    uint32_t display_cols;
}
Notes;

typedef struct
{
    FILE* file;
    uint8_t instrument;
    bool loop;
}
Args;

typedef struct
{
    Audio* audio;
    Notes* notes;
    uint8_t instrument;
}
Consumer;

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
    uint32_t microseconds_per_quarter_note;
}
Meta;

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

static uint64_t
(Time)
(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static float
(ToFreq)
(const uint32_t id)
{
    return 440.0f * powf(2.0f, (id - 69.0f) / 12.0f);
}

static Notes
(Notes_Init)
(void)
{
    Notes notes = { 0 };
    notes.size = 128;
    notes.note = calloc(notes.size, sizeof(*notes.note));
    notes.display_rows = 4;
    notes.display_cols = notes.size / notes.display_rows;
    return notes;
}

static int16_t
(Note_Sin)
(Note* note, const uint32_t id, const uint32_t sample_freq)
{
    const int32_t progress = SDL_AtomicGet(&note->progress);
    const float pi = 3.14159265358979323846f;
    const float freq = ToFreq(id);
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
(*Instruments[])
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

static const uint32_t MAX_INSTRUMENTS = sizeof(Instruments) / sizeof(Instruments[0]);

static const uint32_t AMPLIFICATION = 10;

static void
(Note_On)
(Note* note, const uint8_t note_velocity, const uint8_t channel)
{
    SDL_AtomicSet(&note->gain_setpoint, AMPLIFICATION * note_velocity);
    SDL_AtomicSet(&note->channel, channel);
}

static void
(Note_Clamp)
(Note* note)
{
    const int32_t min = 0;
    const int32_t max = AMPLIFICATION * 128; // MAYBE 127
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
    if(diff)
    {
        const int32_t step = diff / abs(diff);
        SDL_AtomicAdd(&note->gain, step);
    }
}

static Bytes
(Bytes_FromFile)
(FILE* file)
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

static void
(Bytes_Free)
(Bytes* bytes)
{
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}

static void
(Notes_Free)
(Notes* notes)
{
    // PATCH UP DISPLAY FROM CLIPPING WITH PROMPT AT EXIT
    for(uint32_t i = 0; i < notes->display_rows; i++)
        putchar('\n');
    free(notes->note);
    notes->note = NULL;
    notes->size = 0;
}

static uint8_t
(Bytes_U8)
(Bytes* bytes, const uint32_t index)
{
    return bytes->data[index];
}

static uint16_t
(Bytes_U16)
(Bytes* bytes, const uint32_t index)
{
    return Bytes_U8(bytes, index + 0) << 8
         | Bytes_U8(bytes, index + 1);
}

static uint32_t
(Bytes_U32)
(Bytes* bytes, const uint32_t index)
{
    return Bytes_U16(bytes, index + 0) << 16
         | Bytes_U16(bytes, index + 2);
}

static Track
(Track_Init)
(Bytes* bytes, const uint32_t offset, const uint32_t number)
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

static void
(Track_Free)
(Track* track)
{
    free(track->data);
    track->data = NULL;
}

static void
(Track_Back)
(Track* track)
{
    track->index -= 1;
}

static void
(Track_Next)
(Track* track)
{
    track->index += 1;
}

static uint8_t
(Track_U8)
(Track* track)
{
    const uint8_t byte = track->data[track->index];
    Track_Next(track);
    return byte;
}

static uint32_t
(Track_Var)
(Track* track)
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

static char*
(Track_Str)
(Track* track)
{
    const uint32_t len = Track_U8(track);
    char* const str = calloc(len + 1, sizeof(*str));
    for(uint32_t i = 0; i < len; i++)
        str[i] = Track_U8(track);
    return str;
}

static Bytes
(Track_Bytes)
(Track* track)
{
    Bytes bytes = { 0 };
    bytes.size = Track_Var(track);
    bytes.data = calloc(bytes.size, sizeof(*bytes.data));
    for(uint32_t i = 0; i < bytes.size; i++)
        bytes.data[i] = Track_U8(track);
    return bytes;
}

static void
(Track_Crash)
(Track* track)
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

static uint8_t
(Track_Status)
(Track* track, const uint8_t status)
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

static void
(Track_RealEvent)
(Track* track, Notes* notes, const uint8_t leader)
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
        case 0x8:
        {
            const uint8_t note_index = Track_U8(track);
            Track_U8(track);
            assert(note_index < notes->size);
            Note* note = &notes->note[note_index];
            Note_On(note, 0, channel);
            break;
        }
        case 0x9:
        {
            const uint8_t note_index = Track_U8(track);
            const uint8_t note_velocity = Track_U8(track);
            assert(note_index < notes->size);
            Note* note = &notes->note[note_index];
            Note_On(note, note_velocity, channel);
            break;
        }
        case 0xA:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0xB:
        {
            const uint8_t type = Track_U8(track);
            const uint8_t value = Track_U8(track);
            switch(type)
            {
                case 0x07:
                {
                    for(uint32_t i = 0; i < notes->size; i++)
                    {
                        Note* note = &notes->note[i];
                        const int32_t gain_setpoint = SDL_AtomicGet(&note->gain_setpoint);
                        const bool audible = gain_setpoint > 0;
                        if(audible)
                        {
                            const int32_t note_channel = SDL_AtomicGet(&note->channel);
                            if(note_channel == channel)
                                SDL_AtomicSet(&note->gain_setpoint, value * AMPLIFICATION);
                        }
                    }
                    break;
                }
            }
            break;
        }
        case 0xC:
        {
            Track_U8(track);
            break;
        }
        case 0xD:
        {
            Track_U8(track);
            break;
        }
        case 0xE:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
    }
}

static void
(Track_MetaEvent)
(Track* track, Meta* meta)
{
    switch(Track_U8(track))
    {
        default:
        {
            Track_Crash(track);
            break;
        }
        case 0x00:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        {
            free(Track_Str(track));
            break;
        }
        case 0x20:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x21:
        {
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x2F:
        {
            track->run = false;
            break;
        }
        case 0x51:
        {
            Track_U8(track);
            const uint8_t a = Track_U8(track);
            const uint8_t b = Track_U8(track);
            const uint8_t c = Track_U8(track);
            meta->microseconds_per_quarter_note = (a << 16) | (b << 8) | c;
            break;
        }
        case 0x54:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x58:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x59:
        {
            Track_U8(track);
            Track_U8(track);
            Track_U8(track);
            break;
        }
        case 0x7F:
        case 0xF0:
        case 0xF7:
        {
            Bytes bytes = Track_Bytes(track);
            Bytes_Free(&bytes);
            break;
        }
    }
}

static void
(Track_Play)
(Track* track, Notes* notes, Meta* meta)
{
    const int64_t end = -1;
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
                : Track_RealEvent(track, notes, leader);
            // NOTES WITH ZERO DELAY MUST IMMEDIATELY PROCESS
            // THE NEXT NOTE BEFORE MOVING ONTO THE NEXT TRACK.
            Track_Play(track, notes, meta);
        }
    }
}

static Audio
(Audio_Init)
(void)
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

static void
(Audio_Free)
(Audio* audio)
{
    SDL_CloseAudioDevice(audio->dev);
}

static Midi
(Midi_Init)
(Bytes* bytes)
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
(Midi_Free)
(Midi* midi)
{
    for(uint32_t number = 0; number < midi->track_count; number++)
        Track_Free(&midi->track[number]);
    free(midi->track);
    midi->track = NULL;
}

static bool
(Midi_Running)
(Midi* midi)
{
    uint32_t count = 0;
    for(uint32_t i = 0; i < midi->track_count; i++)
        if(midi->track[i].run)
            count += 1;
    return count != 0;
}

static void
(Notes_Draw)
(Notes* notes)
{
    const char* const a = "\x1b[0;32m";
    const char* const b = "\x1b[0;31m";
    const char* const reset = "\x1b[0;0m";
    int32_t id = 0;
    for(uint32_t y = 0; y < notes->display_rows; y++)
    {
        for(uint32_t x = 0; x < notes->display_cols; x++)
        {
            Note* note = &notes->note[id];
            const int32_t gain = SDL_AtomicGet(&note->gain);
            const bool audible = gain > 0;
            printf("%s%4d", audible ? a : b, id);
            id += 1;
        }
        putchar('\n');
    }
    printf("\x1B[%dA", notes->display_rows);
    printf("\r");
    printf(reset);
}

static SDL_atomic_t DONE;

static int32_t
(Audio_Play)
(void* data)
{
    Consumer* consumer = data;
    for(int32_t cycles = 0; SDL_AtomicGet(&DONE) == false; cycles++)
    {
        const uint32_t queue_size = SDL_GetQueuedAudioSize(consumer->audio->dev);
        const uint32_t samples = consumer->audio->spec.samples / 2;
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
                for(uint32_t id = 0; id < consumer->notes->size; id++)
                {
                    Note* note = &consumer->notes->note[id];
                    Note_Roll(note);
                    Note_Clamp(note);
                    const int32_t gain = SDL_AtomicGet(&note->gain);
                    const bool audible = gain > 0;
                    if(audible)
                        mix += Instruments[consumer->instrument](note, id, consumer->audio->spec.freq);
                }
                for(uint32_t channel = 0; channel < consumer->audio->spec.channels; channel++)
                    mixes[sample + channel] = mix;
            }
            SDL_LockAudioDevice(consumer->audio->dev);
            SDL_QueueAudio(consumer->audio->dev, mixes, mixes_size);
            SDL_UnlockAudioDevice(consumer->audio->dev);
            free(mixes);
        }
        if(cycles % 25 == 0)
            Notes_Draw(consumer->notes);
        usleep(500);
    }
    return 0;
}

static uint64_t
(Midi_ToMicrosecondDelay)
(Midi* midi, Meta* meta)
{
    const bool use_ticks_per_beat = (midi->time_division & 0x8000) == 0;
    if(use_ticks_per_beat)
        return meta->microseconds_per_quarter_note / midi->time_division;
    else // FRAMES PER SECOND.
    {
        fprintf(stderr, "FRAMES PER SECOND DELAYS ARE CURRENTLY NOT SUPPORTED\n");
        exit(1);
    }
}

static void
(Midi_Play)
(Midi* midi, Notes* notes, Meta* meta)
{
    float control = 1.0f;
    const float kp = 1e-5;
    const float ki = 1e-8;
    float error_total = 0.0f;
    while(Midi_Running(midi))
    {
        const uint64_t t0 = Time();
        for(uint32_t i = 0; i < midi->track_count; i++)
            Track_Play(&midi->track[i], notes, meta);
        const uint64_t microseconds = Midi_ToMicrosecondDelay(midi, meta);
        usleep(microseconds * control);
        // PI LOOP ENSURES USLEEP IS ACCURATE.
        const uint64_t t1 = Time();
        const float setpoint = microseconds;
        const float actual = t1 - t0;
        const float error = setpoint - actual;
        error_total += error;
        const float p = kp * error;
        const float i = ki * error_total;
        control += p + i;
    }
}

static Args
(Args_Init)
(const int argc, char** argv)
{
    Args args = { 0 };
    args.loop = false;
    args.instrument = 0;
    args.file = NULL;
    if(argc < 2)
    {
        puts("./minimidi <file> <instrument [0,1,2,3...7]> <loop [0, 1]>");
        exit(ERROR_ARGC);
    }
    if(argc >= 2)
        args.file = fopen(argv[1], "rb");
    if(args.file == NULL)
        exit(ERROR_FILE);
    if(argc >= 3)
        args.instrument = atoi(argv[2]) % MAX_INSTRUMENTS;
    if(argc >= 4)
        args.loop = atoi(argv[3]) == 1;
    return args;
}

static void
(Args_Free)
(Args* args)
{
    fclose(args->file);
}

int
(main)
(const int argc, char** argv)
{
    SDL_Init(SDL_INIT_AUDIO);
    Args args = Args_Init(argc, argv);
    Audio audio = Audio_Init();
    Bytes bytes = Bytes_FromFile(args.file);
    Notes notes = Notes_Init();
    Consumer consumer = { &audio, &notes, args.instrument };
    SDL_Thread* thread = SDL_CreateThread(Audio_Play, "MIDI CONSUMER", &consumer);
    do // PRODUCE.
    {
        Midi midi = Midi_Init(&bytes);
        Meta meta = { 0 };
        Midi_Play(&midi, &notes, &meta);
        Midi_Free(&midi);
    }
    while(args.loop);
    SDL_AtomicSet(&DONE, true);
    SDL_WaitThread(thread, NULL);
    Bytes_Free(&bytes);
    Args_Free(&args);
    Audio_Free(&audio);
    Notes_Free(&notes);
    SDL_Quit();
    exit(ERROR_NONE);
}
