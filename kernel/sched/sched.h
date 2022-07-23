#ifndef _SCHED__SCHED_H
#define _SCHED__SCHED_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <sched/proc.h>
#include <lib/elf.h>

#define MAX_RUNNING_THREADS 65536

extern struct process *kernel_process;

noreturn void sched_await(void);
bool sched_enqueue_thread(struct thread *thread, bool by_signal);
struct process *sched_new_process(struct process *old_proc, struct pagemap *pagemap);
struct thread *sched_new_kernel_thread(void *pc, void *arg, bool enqueue);
struct thread *sched_new_user_thread(struct process *proc, void *pc, void *arg, void *sp,
                                     const char **argv, const char **envp, struct auxval *auxval, bool enqueue);

#endif
