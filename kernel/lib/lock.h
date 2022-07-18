#ifndef _LIB__LOCK_H
#define _LIB__LOCK_H

typedef int spinlock_t;

#define SPINLOCK_INIT 0

#define SPINLOCK_ACQUIRE(LOCK) do {                     \
    if (__sync_bool_compare_and_swap(&(LOCK), 0, 1)) {  \
        break;                                          \
    }                                                   \
    asm volatile ("pause");                             \
} while (1)

#define SPINLOCK_RELEASE(LOCK) __sync_bool_compare_and_swap(&(LOCK), 1, 0)

#endif
