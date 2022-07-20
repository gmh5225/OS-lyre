#ifndef _MM__MMAP_H
#define _MM__MMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/vector.h>
#include <mm/vmm.h>
#include <abi-bits/vm-flags.h>
#include <bits/off_t.h>

struct mmap_range_global {
    struct pagemap *shadow_pagemap;
    VECTOR_TYPE(struct mmap_range_local *) locals;
    struct resource *res;
    uintptr_t base;
    size_t length;
    off_t offset;
};

struct mmap_range_local {
    struct pagemap *pagemap;
    struct mmap_range_global *global;
    uintptr_t base;
    size_t length;
    off_t offset;
    int prot;
    int flags;
};

void mmap_list_ranges(struct pagemap *pagemap);
bool mmap_page_in_range(struct mmap_range_global *global, uintptr_t virt,
                            uintptr_t phys, int prot);
bool mmap_range(struct pagemap *pagemap, uintptr_t virt, uintptr_t phys,
                size_t length, int prot, int flags);
void *mmap(struct pagemap *pagemap, uintptr_t addr, size_t length, int prot,
           int flags, struct resource *res, off_t offset);
bool munmap(struct pagemap *pagemap, uintptr_t addr, size_t length);

#endif
