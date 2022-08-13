#ifndef _IPC__SOCKET_H
#define _IPC__SOCKET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <lib/event.h>
#include <lib/resource.h>
#include <abi-bits/socket.h>

enum socket_state {
    SOCKET_CREATED,
    SOCKET_BOUND,
    SOCKET_LISTENING,
    SOCKET_CONNECTED,
    SOCKET_CLOSED,
};

struct socket {
    struct resource;
    struct sockaddr_storage addr;

    struct socket **backlog;
    size_t backlog_max;
    size_t backlog_i;

    struct event connect_event;
    enum socket_state state;
    struct socket *peer;

    int family;
    int type;
    int protocol;

    bool (*bind)(struct socket *this, void *addr, size_t len);
    bool (*connect)(struct socket *this, void *addr, size_t len);
    bool (*peername)(struct socket *this, void *addr, size_t *len);
    bool (*listen)(struct socket *this, int backlog);
    struct resource *(*accept)(struct socket *this);
};

void *socket_create(int family, int type, int protocol, int size);
struct socket *socket_create_unix(int type, int protocol);

#endif
