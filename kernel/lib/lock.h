#ifndef _LIB__LOCK_H
#define _LIB__LOCK_H

typedef int spinlock_t;

#define SPINLOCK_INIT 0

static inline void spinlock_acquire(spinlock_t *lock) {
    for (;;) {
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            break;
        }
        asm volatile ("pause");
    }
}

static inline void spinlock_release(spinlock_t *lock) {
    __sync_bool_compare_and_swap(lock, 1, 0);
}

#endif
