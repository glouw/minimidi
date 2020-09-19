#pragma once

#define TRACK_DRUM_CHANNEL (9)

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

static void Track_RealEvent(Track* track, TrackMeta* meta, Notes* notes, const uint8_t leader)
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
            if(channel != TRACK_DRUM_CHANNEL)
            {
                Note* note = &notes->note[note_index][channel];
                SDL_AtomicSet(&note->gain_setpoint, 0);
            }
            break;
        }
        case 0x9: // NOTE ON.
        {
            const uint8_t note_index = Track_U8(track);
            const uint8_t note_velocity = Track_U8(track);
            if(channel != TRACK_DRUM_CHANNEL)
            {
                Note* note = &notes->note[note_index][channel];
                SDL_AtomicSet(&note->gain_setpoint, NOTE_AMPLIFICATION * note_velocity);
                SDL_AtomicSet(&note->on, true);
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
                    for(uint32_t note_index = 0; note_index < NOTES_MAX; note_index++)
                    {
                        Note* note = &notes->note[note_index][channel];
                        const int32_t gain_setpoint = SDL_AtomicGet(&note->gain_setpoint);
                        const bool audible = gain_setpoint > 0;
                        if(audible)
                            SDL_AtomicSet(&note->gain_setpoint, NOTE_AMPLIFICATION * value);
                    }
                    break;
                }
            }
            break;
        }
        case 0xC: // PROGRAM CHANGE.
        {
            const uint8_t program = Track_U8(track);
            SDL_AtomicSet(&meta->instruments[channel], program);
            break;
        }
        case 0xD: // CHANNEL AFTERTOUCH.
        {
            Track_U8(track);
            break;
        }
        case 0xE: // PITCH BEND.
        {
            Track_U8(track);
            Track_U8(track);
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

static void Track_MetaEvent(Track* track, TrackMeta* meta)
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
            char* const str = Track_Str(track);
            puts(str);
            free(str);
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

static void Track_Play(Track* track, Notes* notes, TrackMeta* meta)
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
