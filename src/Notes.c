#pragma once

typedef struct
{
    Note note[META_NOTES_MAX][META_CHANNEL_MAX];
    uint32_t display_rows;
    uint32_t display_cols;
}
Notes;

static Notes Notes_Init(void)
{
    Notes notes = { 0 };
    notes.display_rows = 32;
    notes.display_cols = META_NOTES_MAX / notes.display_rows;
    return notes;
}

static void Notes_Free(Notes* notes)
{
    // PATCH UP DISPLAY FROM CLIPPING WITH PROMPT AT EXIT.
    for(uint32_t i = 0; i < notes->display_rows; i++)
        putchar('\n');
}

static void Notes_Draw(Notes* notes, Meta* meta)
{
    const char* const grn = "\x1b[0;32m";
    const char* const red = "\x1b[0;31m";
    const char* const nrm = "\x1b[0;00m";
    int32_t note_index = 0;
    const int32_t draw_per_channel = 4;
    for(uint32_t y = 0; y < notes->display_rows; y++)
    {
        for(uint32_t x = 0; x < notes->display_cols; x++)
        {
            bool audible = false;
            for(uint8_t channel = 0; channel < META_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[note_index][channel];
                const int32_t gain = SDL_AtomicGet(&note->gain);
                audible = gain > 0;
                if(audible)
                    break;
            }
            printf("%s%4d", audible ? grn : red, note_index);
            int32_t got = 0;
            for(uint8_t channel = 0; channel < META_CHANNEL_MAX; channel++)
            {
                Note* note = &notes->note[note_index][channel];
                const int32_t gain = SDL_AtomicGet(&note->gain);
                const bool draw = gain > 0;
                if(draw)
                {
                    const uint8_t instrument = SDL_AtomicGet(&meta->instruments[channel]);
                    printf("%3X:%3X:%1X", instrument, gain, channel);
                    got += 1;
                }
            }
            for(int i = 0; i < draw_per_channel - got; i++)
                printf("%9s", "");
            note_index += 1;
        }
        putchar('\n');
    }
    // GO UP BY NUMBER OF DISPLAY ROWS AND LEFTMOST COLUMN.
    printf("\x1B[%dA", notes->display_rows);
    printf("\r");
    printf(nrm);
}
