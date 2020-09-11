#pragma once

typedef struct
{
    FILE* file;
    bool loop;
}
Args;

static Args
(Args_Init)
(const int argc, char** argv)
{
    Args args = { 0 };
    args.loop = false;
    args.file = NULL;
    if(argc < 2 || argc > 3)
    {
        puts("./minimidi <file> <loop [0, 1]>");
        exit(ERROR_ARGC);
    }
    if(argc >= 2)
        args.file = fopen(argv[1], "rb");
    if(args.file == NULL)
        exit(ERROR_FILE);
    if(argc == 3)
        args.loop = atoi(argv[2]) == 1;
    return args;
}

static void
(Args_Free)
(Args* args)
{
    fclose(args->file);
}
