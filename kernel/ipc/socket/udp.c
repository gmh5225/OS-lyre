#include <abi-bits/poll.h>
#include <dev/net/net.h>
#include <lib/errno.h>
#include <lib/print.h>
#include <ipc/socket.h>
#include <netinet/in.h>

// XXX: Poll events may not be correct

struct udp_packet {
    struct net_inetaddr srcip;
    be_uint16_t srcport;
    size_t len;
    uint8_t *data;
};

struct udp_socket {
    struct inetsocket;

    VECTOR_TYPE(struct udp_packet *) packets; // udp packets
};

// should we validate checksums on UDP?
// #define UDP_DOCSUM

static spinlock_t udp_socketslock = SPINLOCK_INIT;
static VECTOR_TYPE(struct udp_socket *) udp_sockets; // keep a reference of all UDP sockets

static bool udp_grabsocket(be_uint16_t port, struct udp_socket **socket) {
    (void)socket; // we are going to use it though

    spinlock_acquire(&udp_socketslock);
    VECTOR_FOR_EACH(&udp_sockets, it,
        struct udp_socket *itsocket = *it;
        if (itsocket->port.value == port.value) {
            *socket = itsocket;
            spinlock_release(&udp_socketslock);
            return true;
        }
    );
    spinlock_release(&udp_socketslock);
    return false;
}

static bool udp_acquireport(struct udp_socket *sock, uint16_t port) {
    if (!port) {
        errno = EINVAL; // invalid port
        return false;
    }

    spinlock_acquire(&udp_socketslock);
    VECTOR_PUSH_BACK(&udp_sockets, sock);
    spinlock_release(&udp_socketslock);
    return true;
}

static ssize_t udp_read(struct resource *_this, struct f_description *description, void *buf, off_t offset, size_t count) {
    (void)offset;
    
    struct udp_socket *this = (struct udp_socket *)_this;

    ssize_t ret = -1;

    if (this->packets.length <= 0) {
        if ((description->flags & O_NONBLOCK) != 0) {
            errno = EWOULDBLOCK;
            goto cleanup;
        }

        struct event *events[] = { &this->event };
        event_await(events, 1, true);
    }

    spinlock_acquire(&this->lock);

    struct udp_packet *packet = VECTOR_ITEM(&this->packets, 0);

    VECTOR_REMOVE(&this->packets, 0);

    if (packet->len < count) {
        count = packet->len;
    }

    memcpy(buf, packet->data, packet->len);

    this->status &= ~POLLIN;
    ret = count;

cleanup:
    spinlock_release(&this->lock);
    return ret;
}

static ssize_t udp_sendpacket(struct net_adapter *adapter, struct net_inetaddr src, struct net_inetaddr dest, be_uint16_t srcport, be_uint16_t destport, const void *buf, size_t len) {
    if (len > adapter->mtu) {
        errno = EMSGSIZE;
        return -1;
    }

    uint8_t *buffer = alloc(adapter->mtu);

    struct net_udpheader *header = (struct net_udpheader *)buffer;

    header->destport = destport;
    header->srcport = srcport;
    header->length.value = __builtin_bswap16(sizeof(struct net_udpheader) + len);
    header->csum.value = 0; // XXX: Checksum
    memcpy(header->data, buf, len);

    return net_sendinet(adapter, src, dest, NET_IPPROTOUDP, header, __builtin_bswap16(header->length.value));
}

static ssize_t udp_write(struct resource *_this, struct f_description *description, const void *buf, off_t offset, size_t count) {
    (void)description;
    (void)offset;

    struct udp_socket *this = (struct udp_socket *)_this;

    ssize_t ret = -1;

    if (this->state != SOCKET_CONNECTED) { // raw socket writes require a peer connection
        errno = EDESTADDRREQ;
        goto cleanup;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)&this->peeraddr;

    if (addr->sin_family != AF_INET) {
        errno = EINVAL;
        goto cleanup;
    }

    struct net_inetaddr destaddr;
    destaddr.value = addr->sin_addr.s_addr;
    be_uint16_t destport;
    destport.value = addr->sin_port; 

    if (!this->port.value) {
        this->port.value = __builtin_bswap16(net_allocport());

        if (!this->port.value) {
            errno = EINTR;
            goto cleanup;
        }
    }

    // XXX: Check for ICMP packet from remote (Destination Unreachable, etc.)
    if (!this->adapter) {
        struct net_macaddr mac;
        struct net_adapter *adapter = NULL;
        struct net_inetaddr thisaddr = (struct net_inetaddr) { .value = INADDR_ANY };
        
        // grab an adapter for this
        int status = net_route(&adapter, thisaddr, destaddr, &mac);
        if (status != 0) {
            return status;
        }

        status = udp_sendpacket(adapter, thisaddr, destaddr, this->port, destport, buf, count);
        if (status != 0) {
            return status;
        }
    } else {
        struct net_inetaddr thisaddr;
        thisaddr.value = ((struct sockaddr_in *)&this->addr)->sin_addr.s_addr;
        int status = udp_sendpacket(this->adapter, thisaddr, destaddr, this->port, destport, buf, count);
        if (status != 0) {
            return status;
        }
    }
   
    this->status |= POLLIN;
cleanup:
    return ret;
}

static ssize_t udp_sendmsg(struct socket *_this, struct f_description *description, const struct msghdr *msg, int flags) {
    (void)description;
    (void)flags;

    struct udp_socket *this = (struct udp_socket *)_this;

    ssize_t ret = -1;

    spinlock_acquire(&this->lock);

    if (this->state != SOCKET_CONNECTED && msg->msg_name == NULL) {
        errno = EDESTADDRREQ;
        goto cleanup;
    }

    struct sockaddr_in *addr = this->state == SOCKET_CONNECTED ? ((struct sockaddr_in *)&this->peeraddr) : (struct sockaddr_in *)msg->msg_name;

    if (addr->sin_family != AF_INET) {
        errno = EINVAL;
        goto cleanup;
    }

    size_t count = 0;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        count += msg->msg_iov[i].iov_len;
    }

    uint8_t *buf = alloc(count);

    size_t transferred = 0;
    size_t remaining = count;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        size_t transfercount = MIN(msg->msg_iov[i].iov_len, remaining);
        memcpy(buf + transferred, msg->msg_iov[i].iov_base, transfercount);
        transferred += transfercount;
        remaining -= transfercount;
    }

    struct net_inetaddr destaddr;
    destaddr.value = addr->sin_addr.s_addr;
    be_uint16_t destport;
    destport.value = addr->sin_port; 

    if (!this->port.value) {
        this->port.value = __builtin_bswap16(net_allocport());

        if (!this->port.value) {
            errno = EINTR;
            goto cleanup;
        }
    }

    // XXX: Check for ICMP packet from remote (Destination Unreachable, etc.)
    if (!this->adapter) {
        struct net_macaddr mac;
        struct net_adapter *adapter = NULL;
        struct net_inetaddr thisaddr = (struct net_inetaddr) { .value = INADDR_ANY };
        
        // grab an adapter for this
        int status = net_route(&adapter, thisaddr, destaddr, &mac);
        if (status != 0) {
            return status;
        }

        status = udp_sendpacket(adapter, thisaddr, destaddr, this->port, destport, buf, count);
        if (status != 0) {
            return status;
        }
    } else {

        struct net_inetaddr thisaddr;
        thisaddr.value = ((struct sockaddr_in *)&this->addr)->sin_addr.s_addr;
        int status = udp_sendpacket(this->adapter, thisaddr, destaddr, this->port, destport, buf, count);
        if (status != 0) {
            return status;
        }
    }

    this->status |= POLLIN;
    ret = transferred;

cleanup:
    spinlock_release(&this->lock);
    return ret;
}

static bool udp_connect(struct socket *_this, struct f_description *description, void *_addr, socklen_t len) {
    (void)description;
    (void)len;

    struct udp_socket *this = (struct udp_socket *)_this;

    struct sockaddr_in *addr = (struct sockaddr_in *)_addr;

    spinlock_acquire(&this->lock);
    // mark address and socket state
    memcpy(&this->peeraddr, addr, sizeof(struct sockaddr_in));

    this->state = SOCKET_CONNECTED;
    this->status |= POLLOUT;
    spinlock_release(&this->lock);

    return true;
}

static ssize_t udp_recvmsg(struct socket *_this, struct f_description *description, struct msghdr *msg, int flags) {
    struct udp_socket *this = (struct udp_socket *)_this;

    ssize_t ret = -1;

    if (this->packets.length <= 0) {
        if (flags & MSG_DONTWAIT) {
            errno = EAGAIN;
            return ret;
        } else {
            if ((description->flags & O_NONBLOCK) != 0) {
                errno = EWOULDBLOCK;
                goto cleanup;
            }

            struct event *events[] = { &this->event };
            event_await(events, 1, true);
        }
    }

    spinlock_acquire(&this->lock);

    struct udp_packet *packet = VECTOR_ITEM(&this->packets, this->packets.length - 1);

    VECTOR_REMOVE(&this->packets, this->packets.length - 1);

    size_t count = 0;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        count += msg->msg_iov[i].iov_len;
    }

    if (packet->len < count) {
        count = packet->len;
    }

    size_t transferred = 0;
    size_t remaining = packet->len;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        size_t transfercount = MIN(msg->msg_iov[i].iov_len, remaining);
        memcpy(msg->msg_iov[i].iov_base, packet->data + transferred, transfercount);
        transferred += transfercount;
        remaining -= transfercount;
    }

    if (msg->msg_name != NULL) {
        socklen_t actual_size = msg->msg_namelen;
        actual_size = sizeof(struct sockaddr_in); // implicit truncate

        struct sockaddr_in addr = (struct sockaddr_in) { .sin_family = AF_INET, .sin_port = packet->srcport.value, .sin_addr.s_addr = packet->srcip.value };
        memcpy(msg->msg_name, &addr, actual_size);
        msg->msg_namelen = actual_size;
    }

    this->status &= ~POLLIN;
    ret = transferred;

cleanup:
    spinlock_release(&this->lock);
    return ret;
}

static bool udp_bind(struct socket *_this, struct f_description *description, void *addr_, socklen_t len) {
    (void)description;
    (void)len;

    struct udp_socket *this = (struct udp_socket *)_this;

    struct sockaddr_in *addr = (struct sockaddr_in *)addr_;
    if (addr->sin_family != AF_INET) {
        errno = EINVAL;
        return false;
    }

    struct net_adapter *a = NULL;
    if (addr->sin_addr.s_addr == INADDR_ANY) { // on all interfaces
        if (this->adapter) {
            net_unbindsocket(this->adapter, (struct socket *)this);
        }

        this->adapter = NULL; // since we want all interfaces
    } else if ((a = net_findadapterbyip((struct net_inetaddr) { .value = addr->sin_addr.s_addr }))) {
        net_bindsocket(a, (struct socket *)this);
    } else {
        errno = -EADDRNOTAVAIL;
        return false;
    }

    memcpy(&this->addr, addr, sizeof(struct sockaddr_in));

    this->port.value = addr->sin_port; // assign listening port
    if (!this->port.value) { // 0 port (bind to any port)
        this->port.value = __builtin_bswap16(net_allocport());
    }

    return udp_acquireport(this, __builtin_bswap16(this->port.value)); // final state success depends on the ability to attain access to this port
}

static void udp_netpacket(struct udp_socket *_this, struct net_inetaddr src, be_uint16_t srcport, void *buf, size_t length) {
    struct udp_packet *packet = alloc(sizeof(struct udp_packet));
    packet->srcip = src;
    packet->srcport = srcport;
    packet->len = length;
    packet->data = alloc(length);
    memcpy(packet->data, buf, length);

    spinlock_acquire(&_this->lock);
    VECTOR_PUSH_BACK(&_this->packets, packet);
    spinlock_release(&_this->lock);

    _this->status |= POLLIN;
    event_trigger(&_this->event, false);
}

void udp_onudp(struct net_adapter *adapter, struct net_inetheader *inetheader, size_t length) {
    (void)adapter;

    if (length < sizeof(struct net_udpheader)) {
        debug_print(0, "net: Discarded [too] short UDP packet (len: %d)\n", length);
        return;
    }

    struct net_udpheader *header = (struct net_udpheader *)inetheader->data;
    if (__builtin_bswap16(header->length.value) > length) {
        debug_print(0, "net: Discarded [too] long UDP packet (len: %d)\n", length);
        return;
    }

    debug_print(0, "net: Received UDP packet from %d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n", NET_PRINTIP(inetheader->src), __builtin_bswap16(header->srcport.value), NET_PRINTIP(inetheader->dest), __builtin_bswap16(header->destport.value));

#ifdef UDP_DOCSUM
    // XXX: Checksum validation
#endif

    struct udp_socket *socket = NULL;
    if (udp_grabsocket(header->destport, &socket)) {
        udp_netpacket(socket, inetheader->src, header->srcport, header->data, __builtin_bswap16(header->length.value));
    } else {
        // reply with ICMP port unreachable
        struct net_icmpheader *icmpreply = alloc(length + sizeof(struct net_inetheader));
        icmpreply->type = 3;
        icmpreply->code = 3;
        icmpreply->csum.value = 0;
        memcpy(icmpreply->data, &inetheader, sizeof(struct net_inetheader));
        memcpy(icmpreply->data + sizeof(struct net_inetheader), inetheader->data, __builtin_bswap16(inetheader->len.value));
        icmpreply->csum = net_checksum(icmpreply, sizeof(struct net_icmpheader) + sizeof(struct net_inetheader) + __builtin_bswap16(inetheader->len.value));
        net_sendinet(adapter, adapter->ip, inetheader->src, NET_IPPROTOICMP, icmpreply, length);
    }
}

static bool udp_unref(struct resource *_this, struct f_description *description) {
    (void)description;

    struct udp_socket *this = (struct udp_socket *)_this;

    this->refcount--;
    if (this->refcount == 0) {
        if (this->adapter) {
            spinlock_acquire(&this->lock);
            net_unbindsocket(this->adapter, (struct socket *)this);
            spinlock_release(&this->lock);
        }

        spinlock_acquire(&udp_socketslock);
        if (VECTOR_FIND(&udp_sockets, this) != VECTOR_INVALID_INDEX) {
            VECTOR_REMOVE_BY_VALUE(&udp_sockets, this);
        }
        spinlock_release(&udp_socketslock);

        net_releaseport(__builtin_bswap16(this->port.value));
    }

    return true;
}

struct socket *socket_create_udp(int type, int protocol) {
    if (protocol != IPPROTO_UDP) {
        errno = EPROTOTYPE;
        return NULL;
    }

    struct udp_socket *sock = socket_create(AF_INET, type, protocol, sizeof(struct udp_socket));
    if (sock == NULL) {
        goto cleanup;
    }

    sock->status = SOCKET_CREATED;
    sock->family = AF_INET;
    sock->type = type;
    sock->protocol = protocol;
    sock->packets = (typeof(sock->packets))VECTOR_INIT;
    sock->stat.st_mode = S_IFSOCK;

    sock->read = udp_read;
    sock->write = udp_write;
    sock->unref = udp_unref;
    sock->ioctl = net_sockioctl;

    sock->bind = udp_bind;
    sock->connect = udp_connect; // connect to socket (will now become default address)
    sock->recvmsg = udp_recvmsg;
    sock->sendmsg = udp_sendmsg;

    return (struct socket *)sock;

cleanup:
    if (sock != NULL) {
        resource_free((struct resource *)sock);
    }
    return NULL;
}
