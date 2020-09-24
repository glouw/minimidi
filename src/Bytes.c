#pragma once

typedef struct
{
    uint8_t* data;
    uint32_t size;
}
Bytes;

static Bytes Bytes_FromFile(FILE* file)
{
    fseek(file, 0, SEEK_END);
    const uint32_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    Bytes bytes = { 0 };
    bytes.size = size;
    bytes.data = calloc(bytes.size, sizeof(*bytes.data));
    for(uint32_t i = 0; i < bytes.size; i++)
        bytes.data[i] = getc(file);
    return bytes;
}

static void Bytes_Free(Bytes* bytes)
{
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}

static uint8_t Bytes_U8(Bytes* bytes, const uint32_t index)
{
    return bytes->data[index];
}

static uint16_t Bytes_U16(Bytes* bytes, const uint32_t index)
{
    return Bytes_U8(bytes, index + 0) << 8 | Bytes_U8(bytes, index + 1);
}

static uint32_t Bytes_U32(Bytes* bytes, const uint32_t index)
{
    return Bytes_U16(bytes, index + 0) << 16 | Bytes_U16(bytes, index + 2);
}
