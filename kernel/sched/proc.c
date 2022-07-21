#include <stdint.h>
#include <stddef.h>
#include <lib/alloc.h>
#include <mm/vmm.h>
#include <sched/proc.h>

// TODO: Replace that with proper scheduling stuff
static struct thread *thread = NULL;

struct thread *sched_current_thread(void) {
    if (thread == NULL) {
        thread  = ALLOC(struct thread);
        thread->process = ALLOC(struct process);
        thread->process->mmap_anon_base = 0x80000000000;
        thread->process->pagemap = vmm_kernel_pagemap;
    }

    return thread;
}
