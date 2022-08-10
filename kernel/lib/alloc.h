#ifndef _LIB__ALLOC_H
#define _LIB__ALLOC_H

#include <stddef.h>
#include <lyre/memstat.h>

#define ALLOC(TYPE, TAG) alloc(sizeof(TYPE), TAG)
#define FREE(ADDR, TAG) free(ADDR, sizeof(*ADDR), TAG)

#define ALLOC_UNKNOWN KMEM_ALLOC_UNKNOWN
#define ALLOC_VECTOR KMEM_ALLOC_VECTOR
#define ALLOC_HASHMAP KMEM_ALLOC_HASHMAP
#define ALLOC_STRING KMEM_ALLOC_STRING
#define ALLOC_PAGEMAP KMEM_ALLOC_PAGEMAP
#define ALLOC_PROCESS KMEM_ALLOC_PROCESS
#define ALLOC_THREAD KMEM_ALLOC_THREAD
#define ALLOC_RESOURCE KMEM_ALLOC_RESOURCE
#define ALLOC_MISC KMEM_ALLOC_MISC
#define ALLOC_TAG_MAX KMEM_ALLOC_TAG_MAX

void *alloc(size_t size, int tag);
void free(void *addr, size_t size, int tag);

#endif
