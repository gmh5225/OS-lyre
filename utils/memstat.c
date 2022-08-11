#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lyre/memstat.h>
#include <lyre/syscall.h>

extern char *__progname;

#define KIB (1024)
#define MIB (1024 * KIB)
#define GIB (1024 * MIB)

static const char *tag_names[] = {
    [KMEM_ALLOC_UNKNOWN] = "Untagged",
    [KMEM_ALLOC_VECTOR] = "Vectors",
    [KMEM_ALLOC_HASHMAP] = "Hash maps",
    [KMEM_ALLOC_STRING] = "Strings",
    [KMEM_ALLOC_PAGEMAP] = "Page maps",
    [KMEM_ALLOC_PROCESS] = "Processes",
    [KMEM_ALLOC_THREAD] = "Threads",
    [KMEM_ALLOC_RESOURCE] = "Resources",
    [KMEM_ALLOC_MISC] = "Miscellaneous"
};

static bool human_readable_sizes = false;

static void print_stat(const char *name, uint64_t amount) {
    char unit[4] = {0};
    size_t total = amount, fraction = 0;

    if (human_readable_sizes) {
        if (amount >= GIB) {
            strcpy(unit, "GiB");
            total = amount / GIB;
            fraction = (amount % GIB) / MIB;
        } else if (amount >= MIB) {
            strcpy(unit, "MiB");
            total = amount / MIB;
            fraction = (amount % MIB) / KIB;
        } else if (amount >= KIB) {
            strcpy(unit, "KiB");
            total = amount / KIB;
            fraction = amount % KIB;
        } else {
            strcpy(unit, "B");
        }

        // Keep only 2 decimal places
        fraction /= 100;
    }

    if (fraction > 0) {
        printf("\t%s: %lu.%lu%s\n", name, total, fraction, unit);
    } else {
        printf("\t%s: %lu%s\n", name, total, unit);
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTION]...\n\n", __progname);
            printf("  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n");
            printf("  --help                display this help and exit\n");
            exit(0);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human-readable") == 0) {
            human_readable_sizes = true;
        } else {
            fprintf(stderr, "%s: unrecognized option '%s'\n", __progname, argv[i]);
            fprintf(stderr, "%s: try '%s --help' for more information\n", __progname, __progname);
            exit(1);
        }
    }

    struct lyre_kmemstat memstat;
    struct __syscall_ret ret = __syscall(SYS_getmemstat, &memstat);

    if ((int)ret.ret == -1) {
        fprintf(stderr, "%s: failed to get memory statistics: %s\n", __progname, strerror(ret.errno));
        exit(1);
    }

    printf("Physical memory statistics:\n");
    print_stat("Total physical memory", memstat.n_phys_total);
    print_stat("Used physical memory", memstat.n_phys_used);
    print_stat("Free physical memory", memstat.n_phys_free);
    print_stat("Reserved physical memory", memstat.n_phys_reserved);

    printf("\nKernel heap statistics:\n");
    for (int i = 0; i < KMEM_ALLOC_TAG_MAX; i++) {
        print_stat(tag_names[i], memstat.n_heap_used[i]);
    }
}
