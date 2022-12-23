#include <stddef.h>
#include <lib/lock.h>
#include <sched/proc.h>

void smartlock_acquire(struct smartlock *smartlock) {
    struct thread *thread = sched_current_thread();
    if (smartlock->thread == thread && smartlock->refcount > 0) {
        goto end;
    }
    while (!CAS(&smartlock->thread, NULL, thread)) {
        asm ("pause");
    }
end:
    smartlock->refcount++;
}

void smartlock_release(struct smartlock *smartlock) {
    struct thread *thread = sched_current_thread();
    if (smartlock->thread != thread) {
        panic(NULL, "Invalid smartlock release");
    }
    if (smartlock->refcount == 0) {
        panic(NULL, "Smartlock release refcount is 0");
    }
    smartlock->refcount--;
    if (smartlock->refcount == 0) {
        smartlock->thread = NULL;
    }
}
