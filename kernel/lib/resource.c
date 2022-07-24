#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/resource.h>
#include <lib/print.h>
#include <sched/proc.h>
#include <abi-bits/fcntl.h>
#include <abi-bits/seek-whence.h>

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

int fdnum_dup(struct process *old_proc, int old_fdnum, struct process *new_proc, int new_fdnum,
              int flags, bool specific, bool cloexec) {
    if (old_proc == NULL) {
        old_proc = sched_current_thread()->process;
    }

    if (new_proc == NULL) {
        new_proc = sched_current_thread()->process;
    }

    if (specific && old_fdnum == new_fdnum && old_proc == new_proc) {
		errno = EINVAL;
		return -1;
	}

    struct f_descriptor *old_fd = fd_from_fdnum(old_proc, old_fdnum);
    if (old_fd == NULL) {
        return -1;
    }

    struct f_descriptor *new_fd = ALLOC(struct f_descriptor);
    if (new_fd == NULL) {
        errno = ENOMEM;
        return -1;
    }

    memcpy(new_fd, old_fd, sizeof(struct f_descriptor));

    new_fdnum = fdnum_create_from_fd(new_proc, new_fd, new_fdnum, specific);
    if (new_fdnum < 0) {
        free(new_fd);
        return -1;
    }

    new_fd->flags = flags & FILE_DESCRIPTOR_FLAGS_MASK;
    if (cloexec) {
        new_fd->flags &= O_CLOEXEC;
    }

    old_fd->description->refcount++;
    old_fd->description->res->refcount++;

    return new_fdnum;
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

int syscall_close(void *_, int fdnum) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    return fdnum_close(proc, fdnum) ? 0 : -1;
}

int syscall_read(void *_, int fdnum, void *buf, size_t count) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return -1;
    }

    struct resource *res = fd->description->res;

    ssize_t read = res->read(res, buf, fd->description->offset, count);
    if (read < 0) {
        return -1;
    }

    fd->description->offset += read;
    return read;
}

int syscall_write(void *_, int fdnum, const void *buf, size_t count) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return -1;
    }

    struct resource *res = fd->description->res;

    ssize_t written = res->write(res, buf, fd->description->offset, count);
    if (written < 0) {
        return -1;
    }

    fd->description->offset += written;
    return written;
}

int syscall_seek(void *_, int fdnum, off_t offset, int whence) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);

    if (fd == NULL) {
        return -1;
    }

    struct f_description *description = fd->description;

    off_t curr_offset = description->offset;
    off_t new_offset = 0;

    switch (whence) {
        case SEEK_CUR:
            new_offset = curr_offset + offset;
            break;
        case SEEK_END:
            new_offset = curr_offset + description->res->stat.st_size;
            break;
        case SEEK_SET:
            new_offset = offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (new_offset < 0) {
        errno = EINVAL;
        return -1;
    }

    // TODO: Implement res->grow
    // if (new_offset >= fd->description->res->stat.st_size) {
    //     description->res->grow(description->res, new_offset);
    // }

    description->offset = new_offset;
    return new_offset;
}

int syscall_fcntl(void *_, int fdnum, uint64_t request, uint64_t arg) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);

    if (fd == NULL) {
        return -1;
    }

    switch (request) {
        case F_DUPFD:
            return fdnum_dup(proc, fdnum, proc, (int)arg, 0, false, false);
        case F_DUPFD_CLOEXEC:
            return fdnum_dup(proc, fdnum, proc, (int)arg, 0, false, true);
        case F_GETFD:
            if ((fd->flags & O_CLOEXEC) != 0) {
                return O_CLOEXEC;
            } else {
                return 0;
            }
        case F_SETFD:
            if ((arg & O_CLOEXEC) != 0) {
                fd->flags = O_CLOEXEC;
            } else {
                fd->flags = 0;
            }
            return 0;
        case F_GETFL:
            return fd->description->flags;
        case F_SETFL:
            fd->description->flags = (int)arg;
            return 0;
        default:
            print("fcntl: Unhandled request %lx\n", request);
            errno = EINVAL;
            return -1;
    }
}

int syscall_dup3(void *_, int old_fdnum, int new_fdnum, int flags) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    return fdnum_dup(proc, old_fdnum, proc, new_fdnum, flags, true, false);
}
