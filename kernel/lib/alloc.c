#include <stddef.h>
#include <stdint.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/print.h>
#include <mm/pmm.h>
#include <mm/slab.h>
#include <mm/vmm.h>

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

int syscall_getmemstat(void *_, struct lyre_kmemstat *buf) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): getmemstat(%lx)", proc->pid, proc->name, buf);

    buf->n_phys_total = pmm_total_pages() * PAGE_SIZE;
    buf->n_phys_free = pmm_free_pages() * PAGE_SIZE;

    for (int tag = 0; tag < ALLOC_TAG_MAX; tag++) {
        buf->n_heap_used[tag] = tagged_allocations[tag];
    }

    return 0;
}
