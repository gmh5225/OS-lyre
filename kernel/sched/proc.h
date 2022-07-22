#ifndef _SCHED__PROC_H
#define _SCHED__PROC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/cpu.h>

struct process {
    uintptr_t mmap_anon_base;
    struct pagemap *pagemap;
};

struct thread {
    struct thread *self;
    struct cpu_local *this_cpu;
    struct process *process;
    int errno;
};

static inline struct thread *sched_current_thread(void) {
    struct thread *ret = NULL;
    asm volatile ("mov %%gs:0x0, %0" : "=r" (ret));
    return ret;
}

#endif
