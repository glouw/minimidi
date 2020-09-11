#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <assert.h>
#include "Error.c"
#include "Bytes.c"
#include "Note.c"
#include "Notes.c"
#include "Args.c"
#include "Audio.c"
#include "Track.c"
#include "Midi.c"

int
(main)
(const int argc, char** argv)
{
    SDL_Init(SDL_INIT_AUDIO);
    Args args = Args_Init(argc, argv);
    Audio audio = Audio_Init();
    Bytes bytes = Bytes_FromFile(args.file);
    Notes notes = Notes_Init();
    AudioConsumer consumer = { &audio, &notes, args.instrument };
    SDL_Thread* thread = SDL_CreateThread(Audio_Play, "MIDI CONSUMER", &consumer);
    do // PRODUCE.
    {
        Midi midi = Midi_Init(&bytes);
        Midi_Play(&midi, &notes);
        Midi_Free(&midi);
    }
    while(args.loop);
    SDL_AtomicSet(&audio.done, true);
    SDL_WaitThread(thread, NULL);
    Bytes_Free(&bytes);
    Args_Free(&args);
    Audio_Free(&audio);
    Notes_Free(&notes);
    SDL_Quit();
    exit(ERROR_NONE);
}
