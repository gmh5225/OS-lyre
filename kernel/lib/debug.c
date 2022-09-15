#include <stdint.h>
#include <lib/debug.h>
#include <lib/lock.h>

uint64_t debug_get_syscall_id(void) {
    static spinlock_t lock = SPINLOCK_INIT;
    static uint64_t syscall_ids = 0;
    spinlock_acquire(&lock);
    uint64_t ret = syscall_ids++;
    spinlock_release(&lock);
    return ret;
}
