#pragma once

typedef struct
{
    Note* note;
    uint32_t size;
    uint32_t display_rows;
    uint32_t display_cols;
}
Notes;

static void
(Notes_Draw)
(Notes* notes)
{
    const char* const grn = "\x1b[0;32m";
    const char* const red = "\x1b[0;31m";
    const char* const nrm = "\x1b[0;00m";
    int32_t id = 0;
    for(uint32_t y = 0; y < notes->display_rows; y++)
    {
        for(uint32_t x = 0; x < notes->display_cols; x++)
        {
            Note* note = &notes->note[id];
            const int32_t gain = SDL_AtomicGet(&note->gain);
            const bool audible = gain > 0;
            printf("%s%4d", audible ? grn : red, id);
            id += 1;
        }
        putchar('\n');
    }
    // GO UP BY NUMBER OF DISPLAY ROWS AND LEFTMOST COLUMN.
    printf("\x1B[%dA", notes->display_rows);
    printf("\r");
    printf(nrm);
}

static Notes
(Notes_Init)
(void)
{
    Notes notes = { 0 };
    notes.size = 128;
    notes.note = calloc(notes.size, sizeof(*notes.note));
    notes.display_rows = 8;
    notes.display_cols = notes.size / notes.display_rows;
    return notes;
}

static void
(Notes_Free)
(Notes* notes)
{
    // PATCH UP DISPLAY FROM CLIPPING WITH PROMPT AT EXIT.
    for(uint32_t i = 0; i < notes->display_rows; i++)
        putchar('\n');
    free(notes->note);
    notes->note = NULL;
    notes->size = 0;
}
