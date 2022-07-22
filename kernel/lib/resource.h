#ifndef _LIB__RESOURCE_H
#define _LIB__RESOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <lib/lock.h>
#include <abi-bits/stat.h>
#include <sys/types.h>

struct resource {
    size_t refcount;
    spinlock_t lock;
    struct stat stat;

    ssize_t (*write)(struct resource *this, void *buf, off_t offset, size_t count);
};

#endif
