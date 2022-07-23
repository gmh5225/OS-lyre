#ifndef _SCHED__PROC_H
#define _SCHED__PROC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/cpu.h>
#include <lib/lock.h>
#include <lib/vector.h>

struct process {
    uintptr_t mmap_anon_base;
    struct pagemap *pagemap;
};

struct thread {
    struct thread *self;
    spinlock_t lock;
    struct cpu_local *this_cpu;
    int running_on;
    bool enqueued;
    bool enqueued_by_signal;
    struct process *process;
    int errno;
    int timeslice;
    spinlock_t yield_await;
    struct cpu_ctx ctx;
    void *gs_base;
    void *fs_base;
    uint64_t cr3;
    void *fpu_storage;
    VECTOR_TYPE(void *) stacks;
    void *pf_stack;
};

static inline struct thread *sched_current_thread(void) {
    struct thread *ret = NULL;
    asm volatile ("mov %%gs:0x0, %0" : "=r" (ret));
    return ret;
}

#endif
