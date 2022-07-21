#ifndef _SCHED__PROC_H
#define _SCHED__PROC_H

#include <stddef.h>
#include <stdint.h>

struct process {
    uintptr_t mmap_anon_base;
    struct pagemap *pagemap;
};

struct thread {
    struct thread *self;
    struct process *process;
    int errno;
};

struct thread *sched_current_thread(void); // {
//     struct thread *ret = NULL;
//     asm volatile ("mov %%gs:0x0, %0" : "=r" (ret));
//     return ret;
// }

#endif
