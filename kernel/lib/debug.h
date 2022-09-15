#ifndef _LIB__DEBUG_H
#define _LIB__DEBUG_H

#include <stdint.h>
#include <lib/print.h>
#include <lib/errno.h>
#include <sched/proc.h>

uint64_t debug_get_syscall_id(void);

#define DEBUG_SYSCALL_ENTER(FMT, ...) \
    uint64_t DEBUG_syscall_id = debug_get_syscall_id(); \
    struct thread *DEBUG_thread = sched_current_thread(); \
    struct process *DEBUG_proc = DEBUG_thread->process; \
    debug_print("\n\e[32m%llu\e[m - %s[%d]: " FMT, DEBUG_syscall_id, DEBUG_proc->name, DEBUG_proc->pid, ## __VA_ARGS__);

#define DEBUG_SYSCALL_LEAVE(FMT, ...) \
    debug_print("\n\e[31m%llu\e[m - %s[%d]: returning " FMT " (errno = %d)", DEBUG_syscall_id, DEBUG_proc->name, DEBUG_proc->pid, ## __VA_ARGS__, errno);

#endif
