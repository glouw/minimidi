#pragma once

#define NOTES_MAX (128)

#define NOTES_CHANNEL_MAX (16)

typedef struct
{
    uint32_t tempo;
    SDL_atomic_t instruments[NOTES_CHANNEL_MAX];
}
TrackMeta;

typedef struct
{
    Note note[NOTES_MAX][NOTES_CHANNEL_MAX];
    uint32_t display_rows;
    uint32_t display_cols;
}
Notes;

static void Notes_Draw(Notes* notes, TrackMeta* meta)
{
    const char* const grn = "\x1b[0;32m";
    const char* const red = "\x1b[0;31m";
    const char* const nrm = "\x1b[0;00m";
    int32_t note_index = 0;
    for(uint32_t y = 0; y < notes->display_rows; y++)
    {
        for(uint32_t x = 0; x < notes->display_cols; x++)
        {
            bool audible = false;
            for(uint8_t channel = 0; channel < NOTES_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[note_index][channel];
                const int32_t gain = SDL_AtomicGet(&note->gain);
                audible = gain > 0;
                if(audible)
                    break;
            }
            printf("%s%4d", audible ? grn : red, note_index);
            int32_t got = 0;
            for(uint8_t channel = 0; channel < NOTES_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[note_index][channel];
                const int32_t gain = SDL_AtomicGet(&note->gain);
                audible = gain > 0;
                if(audible)
                {
                    const uint8_t instrument = SDL_AtomicGet(&meta->instruments[channel]);
                    printf("%4d:%1X", instrument, channel);
                    got += 1;
                }
            }
            for(int i = 0; i < 5 - got; i++)
                printf("      ");
            note_index += 1;
        }
        putchar('\n');
    }
    // GO UP BY NUMBER OF DISPLAY ROWS AND LEFTMOST COLUMN.
    printf("\x1B[%dA", notes->display_rows);
    printf("\r");
    printf(nrm);
}

static Notes Notes_Init(void)
{
    Notes notes = { 0 };
    notes.display_rows = 32;
    notes.display_cols = NOTES_MAX / notes.display_rows;
    return notes;
}

static void Notes_Free(Notes* notes)
{
    // PATCH UP DISPLAY FROM CLIPPING WITH PROMPT AT EXIT.
    for(uint32_t i = 0; i < notes->display_rows; i++)
        putchar('\n');
}
