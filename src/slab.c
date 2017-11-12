#include <string.h>
#include "slab.h"

int slab_new(slab_t *slab, size_t capacity, size_t element_size) {
    slab->entries = (struct slab_entry *) malloc(capacity
            * (sizeof(struct slab_entry) + element_size));
    if (slab->entries == NULL) return -1;

    slab->element_size = element_size;

    slab->capacity = capacity;
    slab->len = 0;
    slab->end = 0;

    slab->next = 0;
    slab->first = -1;
    slab->last = -1;

    return 0;
}

void slab_free(slab_t *slab) {
    free(slab->entries);

    slab->entries = NULL;
    slab->element_size = 0;

    slab->capacity = 0;
    slab->len = 0;
    slab->end = 0;

    slab->next = 0;
    slab->first = -1;
    slab->last = -1;
}

int slab_contains(slab_t *slab, size_t key) {
    struct slab_entry *entry;

    if (key > slab->last) return 0;

    entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
        * key;
    
    return entry->tag == ENTRY_OCCUPIED;
}

void *slab_get(slab_t *slab, size_t key) {
    struct slab_entry *entry;

    entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
        * key;
    
    return entry + sizeof(struct slab_entry);
}

size_t slab_insert(slab_t *slab, const void *element) {
    struct slab_entry *entry, *last_entry, *next_entry, *prev_entry;
    size_t next_vacant, index, i;

    if (slab->next == slab->capacity) return -1;

    slab->len++;
    index = slab->next;
    
    entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
        * slab->next;
    
    next_vacant = entry->next;
    
    entry->tag = ENTRY_OCCUPIED;
    memcpy(entry + sizeof(struct slab_entry), element, slab->element_size);

    for (i = index + 1; i <= slab->last && !slab_contains(slab, i); i++);
    entry->next = slab_contains(slab, i) ? i : (size_t) -1;

    if (entry->next != (size_t) -1) {
        next_entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
            * entry->next;
        next_entry->prev = index;
    }
 
    if (index == 0)
        entry->prev = -1;
    else {
        for (i = index - 1; i >= slab->first && !slab_contains(slab, i); i--);
        entry->prev = i;
    }

    if (entry->prev != (size_t) -1) {
        prev_entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
            * entry->prev;
        prev_entry->next = index;
    }

    if (index == slab->end) {
        if (slab->last != (size_t) -1) {
            last_entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
                * slab->last;
            last_entry->next = index;
        }

        slab->next++;
        slab->end++;

        if (slab->last == (size_t) -1)
            slab->last = 0;
        else
            slab->last++;
    } else {
        slab->next = next_vacant; 
    }

    if (index < slab->first)
        slab->first = index;

    return index;
}

void slab_remove(slab_t *slab, size_t key) {
    struct slab_entry *entry, *next, *prev;
    size_t nextnext;

    entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
        * key;
    
    entry->tag = ENTRY_VACANT;
    
    slab->len = slab->len - 1;
    nextnext = slab->next;
    slab->next = key;

    if (key == slab->first)
        slab->first = entry->next;

    if (entry->prev != (size_t) -1) {
        prev = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
            * entry->prev;
        prev->next = entry->next;
    }
    
    if (entry->next != (size_t) -1) {
        next = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
            * entry->next;
        next->prev = entry->prev;
    }

    entry->next = nextnext;
}

int slab_iter_create(slab_t *slab, slab_iter_t *iter) {
    struct slab_entry *entry;

    if (slab->first == (size_t) -1) return -1;

    entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
        * slab->first;

    iter->index = slab->first;
    iter->next_index = entry->next;
    iter->prev_index = entry->prev;
    iter->data = entry + sizeof(struct slab_entry);

    return 0;
}

int slab_iter_done(slab_iter_t *iter) {
    return iter->index == (size_t) -1;
}

void slab_iter_next(slab_t *slab, slab_iter_t *iter) {
    struct slab_entry *entry;

    if (iter->next_index == (size_t) -1) {
        iter->index = -1;
        iter->next_index = -1;
        iter->prev_index = -1;
        iter->data = NULL;
    } else {
        entry = slab->entries + (sizeof(struct slab_entry) + slab->element_size)
            * iter->next_index;

        iter->prev_index = entry->prev;
        iter->index = iter->next_index;
        iter->next_index = entry->next;
        iter->data = entry + sizeof(struct slab_entry);
    }
}

