#ifndef RIP_H
#define RIP_H

#include <stdio.h>
#include <stdint.h>

#define SAMPLESIZE 1
#define SAMPLERATE 48000

struct rip_metadata {
    char *name;
    char *artist;
    char *album;
    uint32_t length;
};

int rip_parse_string(FILE *f, char **out);

int rip_parse_metadata(FILE *f, struct rip_metadata *metadata);
size_t rip_encode_metadata(const struct rip_metadata *metadata,
                           char **out);
void rip_print_metadata(struct rip_metadata *metadata);
void rip_free_metadata(struct rip_metadata *metadata);

size_t rip_read_chunk(FILE *f, char *out, uint32_t *time);

#endif

