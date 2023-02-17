#ifndef _IPC__SOCKET_H
#define _IPC__SOCKET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <lib/event.h>
#include <lib/resource.h>
#include <abi-bits/socket.h>
#include <dev/net/net.h>

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

    bool (*bind)(struct socket *this, struct f_description *description, void *addr, socklen_t len);
    bool (*connect)(struct socket *this, struct f_description *description, void *addr, socklen_t len);
    bool (*getpeername)(struct socket *this, struct f_description *description, void *addr, socklen_t *len);
    bool (*listen)(struct socket *this, struct f_description *description, int backlog);
    struct socket *(*accept)(struct socket *this, struct f_description *description, struct socket *other, void *addr, socklen_t *len);
    ssize_t (*recvmsg)(struct socket *this, struct f_description *description, struct msghdr *msg, int flags);
    ssize_t (*sendmsg)(struct socket *this, struct f_description *description, const struct msghdr *msg, int flags);
};

struct inetsocket {
    struct socket;
    struct net_adapter *adapter;

    be_uint16_t port; // redundancy
    be_uint16_t destport; // redundancy
    struct sockaddr_storage peeraddr;
    bool localbound;
};

void *socket_create(int family, int type, int protocol, int size);
struct socket *socket_create_unix(int type, int protocol);
struct socket *socket_create_udp(int type, int protocol);
struct socket *socket_create_tcp(int type, int protocol);
bool socket_add_to_backlog(struct socket *sock, struct socket *other);

#endif
