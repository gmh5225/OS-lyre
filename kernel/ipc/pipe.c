#include <stddef.h>
#include <stdint.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/event.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <ipc/pipe.h>
#include <abi-bits/stat.h>

#define PIPE_BUF 4096

struct pipe {
    struct resource;
    void *data;
    size_t capacity;
    size_t read_ptr;
    size_t write_ptr;
    size_t used;
};

ssize_t pipe_read(struct resource *_this, struct f_description *description, void *buf, off_t offset, size_t count) {
    (void)offset;

    struct pipe *this = (struct pipe *)_this;

    ssize_t ret = 0;
    spinlock_acquire(&this->lock);

    if (this->used == 0) {
        if (this->refcount < 2) {
            ret = 0;
            goto cleanup;
        }

        if ((description->flags & O_NONBLOCK) != 0) {
            ret = 0;
            goto cleanup;
        }

        spinlock_release(&this->lock);

        struct event *events[] = {&this->event};
        if (event_await(events, 1, true) < 0) {
            errno = EINTR;
            ret = -1;
            goto cleanup;
        }

        spinlock_acquire(&this->lock);
    }

    if (this->used < count) {
        count = this->used;
    }

    size_t before_wrap = 0, after_wrap = 0, new_ptr = 0;
    if (this->read_ptr + count > this->capacity) {
        before_wrap = this->capacity - this->read_ptr;
        after_wrap = count - before_wrap;
        new_ptr = after_wrap;
    } else {
        before_wrap = count;
        after_wrap = 0;
        new_ptr = this->read_ptr + count;

        if (new_ptr == this->capacity) {
            new_ptr = 0;
        }
    }

    memcpy(buf, this->data + this->read_ptr, before_wrap);
    memcpy(buf + before_wrap, this->data, after_wrap);

    this->read_ptr = new_ptr;
    this->used -= count;

    event_trigger(&this->event, false);
    ret = count;

cleanup:
    spinlock_release(&this->lock);
    return ret;
}

ssize_t pipe_write(struct resource *_this, struct f_description *description, const void *buf, off_t offset, size_t count) {
    (void)description;
    (void)offset;

    struct pipe *this = (struct pipe *)_this;

    ssize_t ret = 0;
    spinlock_acquire(&this->lock);

    if (this->used == this->capacity) {
        spinlock_release(&this->lock);

        struct event *events[] = {&this->event};
        if (event_await(events, 1, true) < 0) {
            errno = EINTR;
            ret = -1;
            goto cleanup;
        }

        spinlock_acquire(&this->lock);
    }

    if (this->used + count > this->capacity) {
        count = this->capacity - this->used;
    }

    size_t before_wrap = 0, after_wrap = 0, new_ptr = 0;
    if (this->write_ptr + count > this->capacity) {
        before_wrap = this->capacity - this->write_ptr;
        after_wrap = count - before_wrap;
        new_ptr = after_wrap;
    } else {
        before_wrap = count;
        after_wrap = 0;
        new_ptr = this->write_ptr + count;

        if (new_ptr == this->capacity) {
            new_ptr = 0;
        }
    }

    memcpy(this->data + this->write_ptr, buf, before_wrap);
    memcpy(this->data, buf + before_wrap, after_wrap);

    this->write_ptr = new_ptr;
    this->used += count;

    event_trigger(&this->event, false);
    ret = count;

cleanup:
    spinlock_release(&this->lock);
    return ret;
}

struct resource *pipe_create(void) {
    struct pipe *pipe = (struct pipe *)resource_create(sizeof(struct pipe));
    if (pipe == NULL) {
        goto fail;
    }

    pipe->read = pipe_read;
    pipe->write = pipe_write;
    pipe->capacity = PIPE_BUF;
    pipe->data = alloc(pipe->capacity);
    pipe->stat.st_mode = S_IFIFO;
    if (pipe->data == NULL) {
        goto fail;
    }

    return (struct resource *)pipe;

fail:
    if (pipe != NULL) {
        if (pipe->data != NULL) {
            free(pipe->data);
        }
        free(pipe);
    }
    return NULL;
}

int syscall_pipe(void *_, int pipe_fdnums[static 2], int flags) {
    (void)_;

    print("syscall: pipe(%lx, %x)", pipe_fdnums, flags);

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct resource *pipe = pipe_create();

    if (pipe == NULL) {
        return -1;
    }

    int read_fd = fdnum_create_from_resource(proc, pipe, flags, 0, false);
    if (read_fd < 0) {
        free(pipe);
        return -1;
    }

    int write_fd = fdnum_create_from_resource(proc, pipe, flags, 0, false);
    if (write_fd < 0) {
        free(pipe);
        return -1;
    }

    pipe_fdnums[0] = read_fd;
    pipe_fdnums[1] = write_fd;
    return 0;
}
