#ifndef _LIB__ALLOC_H
#define _LIB__ALLOC_H

#include <stddef.h>

#define ALLOC(TYPE, TAG) alloc(sizeof(TYPE), TAG)
#define FREE(ADDR, TAG) free(ADDR, sizeof(*ADDR), TAG)

enum {
    ALLOC_UNKNOWN,
    ALLOC_VECTOR,
    ALLOC_HASHMAP,
    ALLOC_STRING,
    ALLOC_PAGEMAP,
    ALLOC_PROCESS,
    ALLOC_THREAD,
    ALLOC_RESOURCE,
    ALLOC_MISC,

    // Keep this variant always at the end
    ALLOC_TAG_MAX,
};

void *alloc(size_t size, int tag);
void free(void *addr, size_t size, int tag);

void alloc_dump_info(void);

#endif
