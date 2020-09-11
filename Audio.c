#pragma once

typedef struct
{
    SDL_AudioSpec spec;
    SDL_AudioDeviceID dev;
    SDL_atomic_t done;
}
Audio;

typedef struct
{
    Audio* audio;
    Notes* notes;
    TrackMeta* meta;
}
AudioConsumer;

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

static int32_t
(Audio_Play)
(void* data)
{
    AudioConsumer* consumer = data;
    uint32_t xor_last = 0x0;
    for(int32_t cycles = 0; SDL_AtomicGet(&consumer->audio->done) == false; cycles++)
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
                for(uint32_t id = 0; id < consumer->notes->size; id++)
                {
                    Note* note = &consumer->notes->note[id];
                    Note_Roll(note);
                    Note_Clamp(note);
                    const int32_t gain = SDL_AtomicGet(&note->gain);
                    const bool audible = gain > 0;
                    if(audible)
                    {
                        const int32_t channel = SDL_AtomicGet(&note->channel);
                        const Instrument instrument = SDL_AtomicGet(&consumer->meta->instruments[channel]);;
                        mix += NOTE_WAVEFORMS[instrument](note, id, consumer->audio->spec.freq);
                    }
                }
                for(uint32_t channel = 0; channel < consumer->audio->spec.channels; channel++)
                    mixes[sample + channel] = mix;
            }
            SDL_LockAudioDevice(consumer->audio->dev);
            SDL_QueueAudio(consumer->audio->dev, mixes, mixes_size);
            SDL_UnlockAudioDevice(consumer->audio->dev);
            free(mixes);
        }
        uint32_t xor = 0x0;
        for(uint32_t id = 0; id < consumer->notes->size; id++)
        {
            Note* note = &consumer->notes->note[id];
            const int32_t gain = SDL_AtomicGet(&note->gain);
            const bool audible = gain > 0;
            if(audible)
                xor ^= id;
        }
        if(xor != xor_last)
            Notes_Draw(consumer->notes);
        xor_last = xor;
        SDL_Delay(1);
    }
    return 0;
}
