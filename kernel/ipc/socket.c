#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <ipc/socket.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <sched/sched.h>
#include <bits/posix/stat.h>
#include <abi-bits/poll.h>

static bool stub_bind(struct socket *this, struct f_description *description, void *addr, socklen_t len) {
    (void)this;
    (void)description;
    (void)addr;
    (void)len;
    errno = ENOSYS;
    return false;
}

static bool stub_connect(struct socket *this, struct f_description *description, void *addr, socklen_t len) {
    (void)this;
    (void)description;
    (void)addr;
    (void)len;
    errno = ENOSYS;
    return false;
}

static bool stub_getpeername(struct socket *this, struct f_description *description, void *addr, socklen_t *len) {
    (void)this;
    (void)description;
    (void)addr;
    (void)len;
    errno = ENOSYS;
    return false;
}

static bool stub_listen(struct socket *this, struct f_description *description, int backlog) {
    (void)this;
    (void)description;
    (void)backlog;
    errno = ENOSYS;
    return false;
}

static struct socket *stub_accept(struct socket *this, struct f_description *description, struct socket *other, void *addr, socklen_t *len) {
    (void)this;
    (void)description;
    (void)other;
    (void)addr;
    (void)len;
    errno = ENOSYS;
    return false;
}

static ssize_t stub_recvmsg(struct socket *this, struct f_description *description, struct msghdr *msg, int flags) {
    (void)this;
    (void)description;
    (void)msg;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

void *socket_create(int family, int type, int protocol, int size) {
    struct socket *sock = resource_create(size);
    if (sock == NULL) {
        goto cleanup;
    }

    sock->stat.st_mode = S_IFSOCK;
    sock->state = SOCKET_CREATED;
    sock->family = family;
    sock->type = type;
    sock->protocol = protocol;

    sock->bind = stub_bind;
    sock->connect = stub_connect;
    sock->getpeername = stub_getpeername;
    sock->listen = stub_listen;
    sock->accept = stub_accept;
    sock->recvmsg = stub_recvmsg;

    return (struct socket *)sock;

cleanup:
    if (sock != NULL) {
        FREE(sock, ALLOC_RESOURCE);
    }
    return NULL;
}

int syscall_socket(void *_, int family, int type, int protocol) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): socket(%d, %d, %d)", proc->pid, proc->name, family, type, protocol);

    struct socket *sock;
    switch (family) {
        case AF_UNIX:
            sock = socket_create_unix(type, protocol);
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (sock == NULL) {
        return -1;
    }

    int flags = 0;
    if (type & SOCK_CLOEXEC) {
        flags |= O_CLOEXEC;
    }
    if (type & SOCK_NONBLOCK) {
        flags |= O_NONBLOCK;
    }

    int ret = fdnum_create_from_resource(proc, (struct resource *)sock, flags, 0, false);
    if (ret == -1) {
        resource_free((struct resource *)sock);
        return -1;
    }

    return ret;
}

int syscall_bind(void *_, int fdnum, void *addr, socklen_t len) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): bind(%d, %lx, %lu)", proc->pid, proc->name, fdnum, addr, len);

    int ret = -1;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return ret;
    }

    struct f_description *desc = fd->description;
    if (S_ISSOCK(desc->res->stat.st_mode)) {
        struct socket *sock = (struct socket *)desc->res;

        if (sock->state != SOCKET_CREATED) {
            errno = EINVAL;
        } else if (sock->bind(sock, desc, addr, len)) {
            sock->state = SOCKET_BOUND;
            ret = 0;
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}

int syscall_connect(void *_, int fdnum, void *addr, socklen_t len) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): connect(%d, %lx, %lu)", proc->pid, proc->name, fdnum, addr, len);

    int ret = -1;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return ret;
    }

    struct f_description *desc = fd->description;
    if (S_ISSOCK(desc->res->stat.st_mode)) {
        struct socket *sock = (struct socket *)desc->res;

        if (sock->state == SOCKET_CONNECTED) {
            errno = EISCONN;
        } else if (sock->state != SOCKET_CREATED) {
            errno = EINVAL;
        } else if (sock->connect(sock, desc, addr, len)) {
            sock->state = SOCKET_CONNECTED;
            ret = 0;
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}

int syscall_listen(void *_, int fdnum, int backlog) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): listen(%d, %d, %lu)", proc->pid, proc->name, fdnum, backlog);

    int ret = -1;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return ret;
    }

    struct f_description *desc = fd->description;
    if (S_ISSOCK(desc->res->stat.st_mode)) {
        struct socket *sock = (struct socket *)desc->res;

        if (sock->state != SOCKET_BOUND) {
            errno = EINVAL;
        } else if (sock->listen(sock, desc, backlog)) {
            sock->backlog_max = backlog;
            sock->backlog = alloc(backlog * sizeof(struct socket *), ALLOC_RESOURCE);
            sock->state = SOCKET_LISTENING;
            ret = 0;
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}

int syscall_accept(void *_, int fdnum, void *addr, socklen_t *len) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): accept(%d, %lx, %lx)", proc->pid, proc->name, fdnum, addr, len);

    int ret = -1;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return ret;
    }

    struct f_description *desc = fd->description;
    if (S_ISSOCK(desc->res->stat.st_mode)) {
        struct socket *sock = (struct socket *)desc->res;

        if (sock->state != SOCKET_LISTENING) {
            errno = EINVAL;
        } else {
            spinlock_acquire(&sock->lock);

            while (sock->backlog_i == 0) {
                sock->status &= ~POLLIN;
                if ((desc->flags & O_NONBLOCK) != 0) {
                    errno = EWOULDBLOCK;
                    goto cleanup;
                }

                spinlock_release(&sock->lock);

                struct event *sock_event = &sock->event;
                ssize_t index = event_await(&sock_event, 1, true);
                if (index == -1) {
                    errno = EINTR;
                    goto cleanup;
                }

                spinlock_acquire(&sock->lock);
            }

            struct socket *other = sock->backlog[0];
            for (size_t i = 1; i < sock->backlog_i; i++) {
                sock->backlog[i - 1] = sock->backlog[i];
            }
            sock->backlog_i--;

            struct socket *peer = sock->accept(sock, desc, other, addr, len);
            if (peer == NULL) {
                goto cleanup;
            }

            other->refcount++;
            other->peer = peer;
            other->state = SOCKET_CONNECTED;
            if (sock->backlog_i == 0) {
                sock->status &= ~POLLIN;
            }

            event_trigger(&other->connect_event, false);

            struct event *conn_event = &sock->connect_event;
            ssize_t index = event_await(&conn_event, 1, true);
            if (index == -1) {
                errno = EINTR;
                goto cleanup;
            }

            ret = fdnum_create_from_resource(proc, (struct resource *)peer, 0, 0, false);

cleanup:
            spinlock_release(&sock->lock);
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}

int syscall_getpeername(void *_, int fdnum, void *addr, socklen_t *len) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): getpeername(%d, %lx, %lx)", proc->pid, proc->name, fdnum, addr, len);

    ssize_t ret = -1;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return ret;
    }

    struct f_description *desc = fd->description;
    if (S_ISSOCK(desc->res->stat.st_mode)) {
        struct socket *sock = (struct socket *)desc->res;

        if (sock->state != SOCKET_CONNECTED) {
            errno = ENOTCONN;
        } else if (sock->getpeername(sock, desc, addr, len)) {
            ret = 0;
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}

ssize_t syscall_recvmsg(void *_, int fdnum, struct msghdr *msg, int flags) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    debug_print("syscall (%d %s): recvmsg(%d, %lx, %d)", proc->pid, proc->name, fdnum, msg, flags);

    ssize_t ret = -1;
    struct f_descriptor *fd = fd_from_fdnum(proc, fdnum);
    if (fd == NULL) {
        return ret;
    }

    struct f_description *desc = fd->description;
    if (S_ISSOCK(desc->res->stat.st_mode)) {
        struct socket *sock = (struct socket *)desc->res;

        if (sock->state != SOCKET_CONNECTED) {
            errno = ENOTCONN;
        } else {
            ret = sock->recvmsg(sock, desc, msg, flags);
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}
