#ifndef _SCHED__PROC_H
#define _SCHED__PROC_H

#include <stddef.h>
#include <stdint.h>

struct process {
    uintptr_t mmap_anon_base;
};

struct thread {
    struct thread *self;
    int errno;
    struct process *process;
};

static inline struct thread *sched_current_thread(void) {
	struct thread *ret = NULL;

    asm volatile ("mov %%gs:0x8, %0" : "=r" (ret));

	return ret;
}

#endif
