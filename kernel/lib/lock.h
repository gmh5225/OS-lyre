#ifndef _LIB__LOCK_H
#define _LIB__LOCK_H

#include <stdbool.h>
#include <lib/misc.h>

typedef int spinlock_t;

#define SPINLOCK_INIT 0

static inline bool spinlock_test_and_acq(spinlock_t *lock) {
    return CAS(lock, 0, 1);
}

static inline void spinlock_acquire(spinlock_t *lock) {
    for (;;) {
        if (spinlock_test_and_acq(lock)) {
            break;
        }
#if defined (__x86_64__)
        asm volatile ("pause");
#endif
    }
}

static inline void spinlock_release(spinlock_t *lock) {
    CAS(lock, 1, 0);
}

#endif
