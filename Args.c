#pragma once

typedef struct
{
    FILE* file;
    uint8_t instrument;
    bool loop;
}
Args;

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
        args.instrument = atoi(argv[2]) % NOTE_MAX_INSTRUMENTS;
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
