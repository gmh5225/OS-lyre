#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/resource.h>

static ssize_t stub_read(struct resource *this, void *buf, off_t offset, size_t count) {
    (void)this;
    (void)buf;
    (void)offset;
    (void)count;
    errno = ENOSYS;
    return -1;
}

static ssize_t stub_write(struct resource *this, const void *buf, off_t offset, size_t count) {
    (void)this;
    (void)buf;
    (void)offset;
    (void)count;
    errno = ENOSYS;
    return -1;
}

static void *stub_mmap(struct resource *this, size_t file_page, int flags) {
    (void)this;
    (void)file_page;
    (void)flags;
    return NULL;
}

void *resource_create(size_t size) {
    struct resource *res = alloc(size);
    if (res == NULL) {
        return NULL;
    }

    res->refcount = 1;
    res->read = stub_read;
    res->write = stub_write;
    res->mmap = stub_mmap;
    return res;
}
