#pragma once

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
(Midi_Done)
(Midi* midi)
{
    uint32_t count = 0;
    for(uint32_t i = 0; i < midi->track_count; i++)
        if(midi->track[i].run)
            count += 1;
    return count == 0;
}

static uint32_t
(Midi_ShaveTicks)
(Midi* midi)
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
(Midi_ToMicrosecondDelay)
(Midi* midi, TrackMeta* meta)
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

static void
(Midi_Play)
(Midi* midi, Notes* notes)
{
    TrackMeta meta = { 0 };
    for(;;)
    {
        for(uint32_t i = 0; i < midi->track_count; i++)
            Track_Play(&midi->track[i], notes, &meta);
        if(Midi_Done(midi))
            break;
        const uint32_t microseconds = Midi_ToMicrosecondDelay(midi, &meta);
        const uint32_t milliseconds = roundf(microseconds / 1000.0f);
        SDL_Delay(milliseconds);
    }
}
