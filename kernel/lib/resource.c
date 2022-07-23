#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/resource.h>
#include <sched/proc.h>

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

bool fdnum_close(struct process *proc, int fdnum) {
    if (proc == NULL) {
        proc = sched_current_thread()->process;
    }

    bool ok = false;
    spinlock_acquire(&proc->fds_lock);

    if (fdnum < 0 || fdnum >= MAX_FDS) {
        errno = EBADF;
        goto cleanup;
    }

    struct f_descriptor *fd = proc->fds[fdnum];
    if (fd == NULL) {
        errno = EBADF;
        goto cleanup;
    }

    if (fd->description->refcount-- == 1) {
        free(fd->description);
    }

    free(fd);

    ok = true;
    proc->fds[fdnum] = NULL;

cleanup:
    spinlock_release(&proc->fds_lock);
    return ok;
}

int fdnum_create_from_fd(struct process *proc, struct f_descriptor *fd, int old_fdnum, bool specific) {
    if (proc == NULL) {
        proc = sched_current_thread()->process;
    }

    int res = -1;
    spinlock_acquire(&proc->fds_lock);

    if (old_fdnum < 0 || old_fdnum >= MAX_FDS) {
        errno = EBADF;
        goto cleanup;
    }

    if (specific) {
        for (int i = old_fdnum; i < MAX_FDS; i++) {
            if (proc->fds[i] == NULL) {
                proc->fds[i] = fd;
                res = i;
                goto cleanup;
            }
        }
    } else {
        // TODO: Close an existing descriptor without deadlocking :^)
        // fdnum_close(proc, old_fdnum);
        proc->fds[old_fdnum] = fd;
        res = old_fdnum;
    }

cleanup:
    spinlock_release(&proc->fds_lock);
    return res;
}

int fdnum_create_from_resource(struct process *proc, struct resource *res, int flags,
                               int old_fdnum, bool specific) {
    struct f_descriptor *fd = fd_create_from_resource(res, flags);
    if (fd == NULL) {
        return -1;
    }

    return fdnum_create_from_fd(proc, fd, old_fdnum, specific);
}

struct f_descriptor *fd_create_from_resource(struct resource *res, int flags) {
    res->refcount++;

    struct f_description *description = ALLOC(struct f_description);
    if (description == NULL) {
        goto fail;
    }

    description->refcount = 1;
    description->flags = flags & FILE_STATUS_FLAGS_MASK;
    description->lock = SPINLOCK_INIT;
    description->res = res;

    struct f_descriptor *fd = ALLOC(struct f_descriptor);
    if (fd == NULL) {
        goto fail;
    }

    fd->description = description;
    fd->flags = flags & FILE_DESCRIPTOR_FLAGS_MASK;
    return fd;

fail:
    res->refcount--;
    if (description != NULL) {
        free(description);
    }
    return NULL;
}

struct f_descriptor *fd_from_fdnum(struct process *proc, int fdnum) {
    if (proc == NULL) {
        proc = sched_current_thread()->process;
    }

    struct f_descriptor *ret = NULL;
    spinlock_acquire(&proc->fds_lock);

    if (fdnum < 0 || fdnum >= MAX_FDS) {
        errno = EBADF;
        goto cleanup;
    }

    ret = proc->fds[fdnum];
    if (ret == NULL) {
        errno = EBADF;
        goto cleanup;
    }

    ret->description->refcount++;

cleanup:
    spinlock_release(&proc->fds_lock);
    return ret;
}
