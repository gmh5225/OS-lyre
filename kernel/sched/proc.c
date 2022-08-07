#include <stdint.h>
#include <stddef.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/hashmap.h>
#include <lib/print.h>
#include <mm/vmm.h>
#include <sched/proc.h>
#include <abi-bits/utsname.h>

static struct smartlock futex_lock = SMARTLOCK_INIT;
static HASHMAP_TYPE(struct event *) futex_hashmap = HASHMAP_INIT(256);

void proc_init(void) {
    uintptr_t phys = 0;
    HASHMAP_INSERT(&futex_hashmap, &phys, sizeof(phys), NULL);
}

int syscall_uname(void *_, struct utsname *buffer) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    print("syscall (%d %s): uname(%lx)", proc->pid, proc->name, buffer);

	strncpy(buffer->sysname, "Lyre", sizeof(buffer->sysname));
	strncpy(buffer->nodename, "lyre", sizeof(buffer->nodename));
	strncpy(buffer->release, "0.0.1", sizeof(buffer->release));
	strncpy(buffer->version, __DATE__ " " __TIME__, sizeof(buffer->version));
    return 0;
}

int syscall_futex_wait(void *_, int *ptr, int expected) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    print("syscall (%d %s): futex_wait(%lx, %d)", proc->pid, proc->name, ptr, expected);

    if (*ptr != expected) {
        errno = EAGAIN;
        return -1;
    }

    uintptr_t phys = vmm_virt2phys(proc->pagemap, (uintptr_t)ptr);

    smartlock_acquire(&futex_lock);

    struct event *event = NULL;
    if (!HASHMAP_GET(&futex_hashmap, event, &phys, sizeof(phys))) {
        event = ALLOC(struct event);
        HASHMAP_INSERT(&futex_hashmap, &phys, sizeof(phys), event);
    }

    smartlock_release(&futex_lock);

    ssize_t ret = event_await(&event, 1, true);
    if (ret == -1) {
        errno = EINTR;
        return -1;
    }

    return 0;
}

int syscall_futex_wake(void *_, int *ptr) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    print("syscall (%d %s): futex_wake(%lx)", proc->pid, proc->name, ptr);

    // Make sure the page isn't demand paged
    *(volatile int *)ptr;

    uintptr_t phys = vmm_virt2phys(proc->pagemap, (uintptr_t)ptr);

    smartlock_acquire(&futex_lock);

    struct event *event = NULL;
    if (!HASHMAP_GET(&futex_hashmap, event, &phys, sizeof(phys))) {
        smartlock_release(&futex_lock);
        return 0;
    }

    event_trigger(event, true);
    smartlock_release(&futex_lock);
    return 0;
}

mode_t syscall_umask(void *_, mode_t mask) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    print("syscall (%d %s): umask(%o)", proc->pid, proc->name, mask);

    mode_t old_mask = proc->umask;
    proc->umask = mask;
    return old_mask;
}
