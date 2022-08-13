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

static bool stub_bind(struct socket *this, void *addr, size_t len) {
    (void)this;
    (void)addr;
    (void)len;
    panic(NULL, true, "Stub socket bind function called");
}

static bool stub_connect(struct socket *this, void *addr, size_t len) {
    (void)this;
    (void)addr;
    (void)len;
    panic(NULL, true, "Stub socket connect function called");
}

static bool stub_peername(struct socket *this, void *addr, size_t *len) {
    (void)this;
    (void)addr;
    (void)len;
    panic(NULL, true, "Stub socket peername function called");
}

static bool stub_listen(struct socket *this, int backlog) {
    (void)this;
    (void)backlog;
    panic(NULL, true, "Stub socket listen function called");
    return false;
}

static struct resource *stub_accept(struct socket *this) {
    (void)this;
    panic(NULL, true, "Stub socket accept function called");
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
    sock->peername = stub_peername;
    sock->listen = stub_listen;
    sock->accept = stub_accept;

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

int syscall_bind(void *_, int fdnum, void *addr, size_t len) {
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
        } else if (sock->bind(sock, addr, len)) {
            sock->state = SOCKET_BOUND;
            ret = 0;
        }
    } else {
        errno = ENOTSOCK;
    }

    desc->res->unref(desc->res, desc);
    return ret;
}

int syscall_connect(void *_, int fdnum, void *addr, size_t len) {
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
        } else if (sock->connect(sock, addr, len)) {
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
        } else if (sock->listen(sock, backlog)) {
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

int syscall_accept(void *_) {
    (void)_;
    panic(NULL, true, "uwu accept");
}
