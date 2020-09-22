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
    Meta* meta;
}
AudioConsumer;

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
    AudioConsumer* consumer = data;
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
                for(uint32_t note_index = 0; note_index < META_NOTES_MAX; note_index++)
                {
                    for(uint8_t channel = 0; channel < META_CHANNEL_MAX; channel++)
                    {
                        Note* note = &consumer->notes->note[note_index][channel];
                        if(SDL_AtomicGet(&note->on))
                        {
                            Note_Roll(note);
                            Note_Clamp(note);
                            const int32_t gain = SDL_AtomicGet(&note->gain);
                            const bool audible = gain > 0;
                            if(audible)
                            {
                                const uint8_t instrument = SDL_AtomicGet(&consumer->meta->instruments[channel]);
                                mix += NOTE_WAVEFORMS[instrument](note, consumer->meta, channel, note_index, consumer->audio->spec.freq);
                            }
                        }
                    }
                }
                mix *= META_AUDIO_AMPLIFICATION;
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
