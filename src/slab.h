#ifndef SLAB_H
#define SLAB_H

#include <stdlib.h>

enum slab_entry_tag {
    ENTRY_OCCUPIED,
    ENTRY_VACANT
};

struct slab_entry {
    enum slab_entry_tag tag;
    size_t next;
    size_t prev;
};

typedef struct slab {
    struct slab_entry *entries;
    size_t element_size;

    size_t capacity;
    size_t len;
    size_t end;

    size_t next;
    size_t first;
    size_t last;
} slab_t;

typedef struct slab_iter {
    size_t index;
    size_t next_index;
    size_t prev_index;
    void *data;
} slab_iter_t;


int slab_new(slab_t *slab, size_t capacity, size_t element_size);
void slab_free(slab_t *slab);
int slab_contains(slab_t *slab, size_t key);
void *slab_get(slab_t *slab, size_t key);
size_t slab_insert(slab_t *slab, const void *element);
void slab_remove(slab_t *slab, size_t key);

int slab_iter_create(slab_t *slab, slab_iter_t *iter);
int slab_iter_done(slab_iter_t *iter);
void slab_iter_next(slab_t *slab, slab_iter_t *iter);

#endif

