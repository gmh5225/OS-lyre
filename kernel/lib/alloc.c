#include <stddef.h>
#include <stdint.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/print.h>
#include <mm/slab.h>

static size_t tagged_allocations[ALLOC_TAG_MAX] = {0};

void *alloc(size_t size, int tag) {
    void *result = slab_alloc(size);
    if (result != NULL) {
        tagged_allocations[tag] += size;
    } else {
        errno = ENOMEM;
    }
    return result;
}

void free(void *addr, size_t size, int tag) {
    if (addr == NULL) {
        return;
    }

    ASSERT(tagged_allocations[tag] >= size);
    tagged_allocations[tag] -= size;
    slab_free(addr);
}

#define KIB (1024)
#define MIB (1024 * KIB)
#define GIB (1024 * MIB)

static const char *tag_names[] = {
    [ALLOC_UNKNOWN] = "Untagged",
    [ALLOC_VECTOR] = "Vectors",
    [ALLOC_HASHMAP] = "Hash maps",
    [ALLOC_STRING] = "Strings",
    [ALLOC_PAGEMAP] = "Page maps",
    [ALLOC_PROCESS] = "Processes",
    [ALLOC_THREAD] = "Threads",
    [ALLOC_RESOURCE] = "Resources",
    [ALLOC_MISC] = "Miscellaneous"
};

static void dump_tagged_allocation_info(int tag) {
    char unit[4] = "XiB";

    size_t total = tagged_allocations[tag], amount = 0, fraction = 0;
    if (total >= GIB) {
        unit[0] = 'G';
        amount = total / GIB;
        fraction = (total % GIB) / MIB;
    } else if (total >= MIB) {
        unit[0] = 'M';
        amount = total / MIB;
        fraction = (total % MIB) / KIB;
    } else if (total >= KIB) {
        unit[0] = 'K';
        amount = total / KIB;
        fraction = total % KIB;
    } else {
        unit[0] = 'B';
        unit[1] = 0;
        amount = total;
    }

    // Keep only 2 decimal places
    fraction /= 100;

    if (fraction > 0) {
        kernel_print("   %s: %lu.%lu%s (%luB)\n", tag_names[tag], amount, fraction, unit, total);
    } else {
        kernel_print("   %s: %lu%s (%luB)\n", tag_names[tag], amount, unit, total);
    }
}

void alloc_dump_info(void) {
    kernel_print("alloc: Allocation info:\n");

    for (int tag = 0; tag < ALLOC_TAG_MAX; tag++) {
        dump_tagged_allocation_info(tag);
    }
}
