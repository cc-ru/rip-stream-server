#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <byteswap.h>

#include "rip.h"

#define IS_LITTLE_ENDIAN (1 == *(unsigned char *)&(const int){1})

int rip_parse_string(FILE *f, char **out) {
    int count;
    uint16_t len;

    count = fread(&len, 1, 2, f);
    if (count != 2) {
        if (errno != 0)
            perror("rip_parse");
        else
            fprintf(stderr, "rip_parse_metadata: unexpected EOF\n");
        return -1;
    }

    if (IS_LITTLE_ENDIAN)
        len = __bswap_16(len); 

    *out = (char *) calloc(len + 1, 1);

    count = fread(*out, 1, len, f);
    if (count != len) {
        if (errno != 0)
            perror("rip_parse");
        else
            fprintf(stderr, "rip_parse_metadata: unexpected EOF\n");
        return -1;
    }

    return 0;
}

int rip_parse_metadata(FILE *f, struct rip_metadata *metadata) {
    int status, count;
    char signature[4];
    signature[3] = 0;

    count = fread(signature, 1, 3, f);
    if (count != 3) {
        if (errno != 0)
            perror("rip_parse");
        else
            fprintf(stderr, "rip_parse_metadata: unexpected EOF\n");
        return -1;
    }

    if (strcmp(signature, "rip") != 0) {
        fprintf(stderr, "rip_parse_metadata: bad signature\n");
        return -1;
    }

    status = rip_parse_string(f, &metadata->name);
    if (status == -1) return -1;

    status = rip_parse_string(f, &metadata->artist);
    if (status == -1) return -1;

    status = rip_parse_string(f, &metadata->album);
    if (status == -1) return -1;

    count = fread(&metadata->length, 1, 4, f);
    if (count == 0) {
        fprintf(stderr, "rip_parse_metadata: unexpected EOF\n");
        return -1;
    } else if (count != 4) {
        if (errno != 0)
            perror("rip_parse_metadata");
        else
            fprintf(stderr, "rip_parse_metadata: unexpected EOF\n");
        return -1;
    }

    if (IS_LITTLE_ENDIAN)
        metadata->length = __bswap_32(metadata->length);

    metadata->length = metadata->length * 8 / SAMPLESIZE / SAMPLERATE * 100;

    return 0;
}

size_t rip_encode_metadata(const struct rip_metadata *metadata,
                           char **out)
{
    size_t name_lens, artist_lens, album_lens;

    size_t name_len = strlen(metadata->name),
           artist_len = strlen(metadata->artist),
           album_len = strlen(metadata->album);

    size_t len = 11 + name_len + artist_len + album_len;

    if (IS_LITTLE_ENDIAN) {
        name_lens = __bswap_16(name_len);
        artist_lens = __bswap_16(artist_len);
        album_lens = __bswap_16(album_len);
    } else {
        name_lens = name_len;
        artist_lens = artist_len;
        album_lens = album_len;
    }

    *out = (char *) malloc(len);
    if (out == NULL) return -1;

    *out[0] = 1;
    
    memcpy(*out + 1, &metadata->length, 4);
    if (IS_LITTLE_ENDIAN)
        *(uint32_t *) (*out + 1) = __bswap_32(*(uint32_t *) (*out + 1));

    memcpy(*out + 5, &name_lens, 2);
    memcpy(*out + 7, metadata->name, name_len);

    memcpy(*out + 7 + name_len, &artist_lens, 2);
    memcpy(*out + 9 + name_len, metadata->artist, artist_len);

    memcpy(*out + 9 + name_len + artist_len, &album_lens, 2);
    memcpy(*out + 11 + name_len + artist_len, metadata->album, album_len);

    return len;
}

void rip_print_metadata(struct rip_metadata *metadata) {
    printf("%s (%s) - %s [%u cs]", metadata->artist, metadata->album,
            metadata->name, metadata->length);
}

void rip_free_metadata(struct rip_metadata *metadata) {
    free(metadata->name);
    free(metadata->artist);
    free(metadata->album);
}

size_t rip_read_chunk(FILE *f, char *out, uint32_t *time) {
    size_t count = fread(out + 9, 1, SAMPLESIZE * SAMPLERATE / 8, f);

    if (count == 0) {
        if (feof(f))
            return 0;
        else
            perror("rip_read_chunk");
        return -1;
    }

    out[0] = 2;

    memcpy(out + 1, &count, 4);
    if (IS_LITTLE_ENDIAN)
        *(uint32_t *) (out + 1) = __bswap_32(*(uint32_t *) (out + 1));

    memcpy(out + 5, time, 4);
    if (IS_LITTLE_ENDIAN)
        *(uint32_t *) (out + 5) = __bswap_32(*(uint32_t *) (out + 5));

    *time += count * 8 / SAMPLESIZE / SAMPLERATE * 100;

    return count + 9;
}

