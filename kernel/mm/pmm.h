#ifndef _MM__PMM_H
#define _MM__PMM_H

#include <stddef.h>

void pmm_init(void);
void *pmm_alloc(size_t pages);
void *pmm_alloc_nozero(size_t pages);
void pmm_free(void *addr, size_t pages);

#endif
