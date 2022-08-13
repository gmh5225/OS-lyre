#include <stdint.h>
#include <stddef.h>
#include <fs/vfs/vfs.h>
#include <ipc/socket.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <sched/sched.h>
#include <sys/un.h>

struct unix_socket {
    struct socket;

    void *data;
    size_t capacity;
    size_t read_ptr;
    size_t write_ptr;
    size_t used;
};

static bool unix_bind(struct socket *this, void *addr_, size_t len) {
    (void)len;

    struct sockaddr_un *addr = (struct sockaddr_un *)addr_;
    if (addr->sun_family != AF_UNIX) {
        errno = EINVAL;
        return false;
    }

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct vfs_node *node = vfs_create(proc->cwd, addr->sun_path, S_IFSOCK);
    if (node == NULL) {
        return false;
    }

    this->stat = node->resource->stat;
    node->resource = (struct resource *)this;
    memcpy(&this->addr, addr, sizeof(struct sockaddr_un));
    return true;
}

static bool unix_listen(struct socket *this_, int backlog) {
    (void)this_;
    (void)backlog;
    return true;
}

struct socket *socket_create_unix(int type, int protocol) {
    if (type != SOCK_STREAM) {
        errno = EINVAL;
        return NULL;
    }

    struct unix_socket *sock = socket_create(AF_UNIX, type, protocol, sizeof(struct unix_socket));
    if (sock == NULL) {
        goto cleanup;
    }

    sock->stat.st_mode = S_IFSOCK;
    sock->state = SOCKET_CREATED;
    sock->family = AF_UNIX;
    sock->type = type;
    sock->protocol = protocol;

    sock->bind = unix_bind;
	sock->listen = unix_listen;
	// bool (*connect)(struct socket *this, void *addr, size_t addrlen);
	// bool (*peername)(struct socket *this, void *addr, size_t *addrlen);
	// struct resource *(*accept)(struct socket *this);

    return (struct socket *)sock;

cleanup:
    if (sock != NULL) {
        resource_free((struct resource *)sock);
    }
    return NULL;
}
