#pragma once

typedef struct
{
    SDL_atomic_t gain;
    SDL_atomic_t gain_setpoint;
    SDL_atomic_t progress;
    SDL_atomic_t on;
    // PRIVATE.
    int32_t _bend_last;
    float _id;
    bool _wait;
    bool _was_init;
}
Note;

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
        {
            note->_was_init = false;
            SDL_AtomicSet(&note->on, false);
        }
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
