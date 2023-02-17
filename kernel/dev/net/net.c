#include <dev/net/net.h>
#include <ipc/socket.h>
#include <lib/bitmap.h>
#include <lib/errno.h>
#include <lib/event.h>
#include <lib/lock.h>
#include <lib/print.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/in.h>
#include <printf.h>
#include <sched/sched.h>
#include <time/time.h>

static int net_ethcount = 0;
static VECTOR_TYPE(struct net_adapter *) net_adapters = VECTOR_INIT;
static uint8_t *net_portbitmap; // use a bitmap to keep track of port allocations
static spinlock_t net_portbitmaplock = SPINLOCK_INIT;

be_uint16_t net_checksum(void *data, size_t length) {
    uint16_t *p = (uint16_t *)data;
    uint32_t csum = 0;

    for (size_t i = length; i >= 2; i -= 2) {
        csum += *p++;
    }

    csum = (csum & 0xFFFF) + (csum >> 16);
    if (csum > UINT16_MAX) {
        csum += 1;
    }

    be_uint16_t ret;
    ret.value = ~csum;
    return ret;
}

struct net_adapter *net_findadapterbyip(struct net_inetaddr addr) {
    VECTOR_FOR_EACH(&net_adapters, it,
        struct net_adapter *adapter = *it;
        if (adapter->ip.value == addr.value) {
            return adapter;
        }
    );

    return NULL;
}

static bool net_grabcache(struct net_adapter *adapter, struct net_inetaddr ip, struct net_macaddr *mac) {
    spinlock_acquire(&adapter->addrcachelock);
    VECTOR_FOR_EACH(&adapter->addrcache, it,
        struct net_inethwpair *pair = *it;
        if (pair->inet.value == ip.value) {
            *mac = pair->hw;
            spinlock_release(&adapter->addrcachelock);
            return true; // found us a cached record
        }
    );
    spinlock_release(&adapter->addrcachelock);
    return false;
}

static bool net_findcache(struct net_adapter *adapter, struct net_inetaddr ip) {
    spinlock_acquire(&adapter->addrcachelock);
    VECTOR_FOR_EACH(&adapter->addrcache, it,
        struct net_inethwpair *pair = *it;
        if (pair->inet.value == ip.value) {
            spinlock_release(&adapter->addrcachelock);
            return true; // found us a cached record
        }
    );
    spinlock_release(&adapter->addrcachelock);
    return false;
}

uint16_t net_allocport(void) {
    spinlock_acquire(&net_portbitmaplock);
    for (size_t i = 0; i < NET_PORTRANGEEND - NET_PORTRANGESTART; i++) {
        if (net_portbitmap[i] == 0xff) {
            continue;
        }

        for (size_t bit = 0; bit < 8; bit++) {
            if (bitmap_test(net_portbitmap, bit + (i * 8)) == false) {
                bitmap_set(net_portbitmap, bit + (i * 8));
                spinlock_release(&net_portbitmaplock);
                return bit + (i * 8) + NET_PORTRANGESTART;
            }
        }
    }
    spinlock_release(&net_portbitmaplock);

    debug_print(0, "net: Could not allocate port\n");
    return 0;
}

void net_releaseport(uint16_t port) {
    bitmap_reset(net_portbitmap, port);
}

void net_bindsocket(struct net_adapter *adapter, struct socket *sock) {
    spinlock_acquire(&adapter->socklock);

    ((struct inetsocket *)sock)->adapter = adapter;
    VECTOR_PUSH_BACK(&adapter->boundsocks, sock);

    spinlock_release(&adapter->socklock);
}

void net_unbindsocket(struct net_adapter *adapter, struct socket *sock) {
    spinlock_acquire(&adapter->socklock);

    VECTOR_REMOVE_BY_VALUE(&adapter->boundsocks, sock);
    ((struct inetsocket *)sock)->adapter = NULL;

    spinlock_release(&adapter->socklock);
}

void net_unbindall(struct net_adapter *adapter) {
    spinlock_acquire(&adapter->socklock);

    while (adapter->boundsocks.length) {
        struct inetsocket *sock = (struct inetsocket *)VECTOR_ITEM(&adapter->boundsocks, adapter->boundsocks.length - 1);
        sock->adapter = NULL;
        VECTOR_REMOVE(&adapter->boundsocks, adapter->boundsocks.length - 1);
    }

    spinlock_release(&adapter->socklock);
}

ssize_t net_sendinet(struct net_adapter *adapter, struct net_inetaddr src, struct net_inetaddr dest, uint8_t protocol, void *data, size_t length) {
    if (length > adapter->mtu - sizeof(struct net_etherframe) - sizeof(struct net_inetheader)) {
        errno = -EMSGSIZE;
        return -1;
    }

    uint8_t *buffer = alloc(sizeof(struct net_etherframe) + sizeof(struct net_inetheader) + length); // since ICMP is variable size
    struct net_etherframe *frame = (struct net_etherframe *)buffer;
    frame->type.value = __builtin_bswap16(NET_ETHPROTOIPV4); // IPv4
    frame->src = adapter->mac;

    if (dest.value == INADDR_BROADCAST) {
        frame->dest = (struct net_macaddr) { .mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };
    } else {
        ssize_t status = net_route(&adapter, src, dest, &frame->dest);
        if (status != 0) {
            return status;
        }
    }

    struct net_inetheader *ipheader = (struct net_inetheader *)frame->data;
    memset(ipheader, 0, sizeof(struct net_inetheader));

    ipheader->ihl = 5;
    ipheader->version = 4; // IPv4
    ipheader->len.value = __builtin_bswap16(sizeof(struct net_inetheader) + length);
    ipheader->flags = 0;
    ipheader->ttl = 64;
    ipheader->id.value = __builtin_bswap16(adapter->ipframe++);
    ipheader->protocol = protocol;
    ipheader->csum.value = 0;
    ipheader->dest = dest; // reply destination
    ipheader->src = adapter->ip;

    ipheader->csum = net_checksum(ipheader, sizeof(struct net_inetheader));

    memcpy(ipheader->data, data, length);

    adapter->txpacket(adapter, buffer, sizeof(struct net_etherframe) + sizeof(struct net_inetheader) + length);
    return 0;
}

ssize_t net_lookup(struct net_adapter *adapter, struct net_inetaddr ip, struct net_macaddr *mac) {
    if (ip.value == NET_IPSTRUCT(NET_IP(127, 0, 0, 1)).value) { // loopback address
        *mac = NET_MACSTRUCT(0, 0, 0, 0, 0, 0); // simply just set a blank MAC and skip (since lookups are not good for us)
        return 0;
    }

    struct net_adapter *a = NULL;
    if ((a = net_findadapterbyip(ip))) { // check if it's a local address, in which case we can skip this step entirely
        *mac = a->mac;
        return 0;
    }

    if (net_grabcache(adapter, ip, mac)) { // already have an ARP cache for this
        return 0;
    }

    uint8_t *buffer = alloc(sizeof(struct net_etherframe) + sizeof(struct net_arpheader));

    struct net_etherframe *frame = (struct net_etherframe *)buffer;
    frame->type.value = __builtin_bswap16(NET_ETHPROTOARP);
    frame->src = adapter->mac;
    frame->dest = (struct net_macaddr) { .mac = { 0xff, 0xff, 0xff, 0xff, 0xff } }; // broadcast request

    struct net_arpheader *arp = (struct net_arpheader *)frame->data;

    arp->desthw = frame->dest; // same thing, so copy
    arp->srchw = frame->src; // ditto
    arp->hwlen = 6;
    arp->hwtype.value = __builtin_bswap16(adapter->type);

    arp->destpr = ip;
    arp->srcpr = adapter->ip;
    arp->plen = 4;
    arp->prtype.value = __builtin_bswap16(NET_ETHPROTOIPV4); // IPv4

    arp->opcode.value = __builtin_bswap16(1); // request

    adapter->txpacket(adapter, buffer, sizeof(struct net_etherframe) + sizeof(struct net_arpheader));
    if (net_findcache(adapter, ip)) {
        goto skipcase; // in case we (impossibly) manage to get an instant response handle it
    }

    // XXX: Think of a better timeout delay
    size_t timeout = 500;
    while (timeout > 0) {
        if (net_findcache(adapter, ip)) {
            break;
        }
        timeout--;
        time_msleep(10);
    }

    if (!net_findcache(adapter, ip)) {
        debug_print(0, "net: Timeout on ARP response\n");
        errno = ENETUNREACH;
        return -1;
    }
skipcase:
    ASSERT_MSG(net_grabcache(adapter, ip, mac), "net: ARP reply broken");
    return 0;
}

ssize_t net_route(struct net_adapter **adapter, struct net_inetaddr local, struct net_inetaddr remote, struct net_macaddr *mac) {
    bool islocal = false;

    if (*adapter) {
        if (local.value != INADDR_ANY && local.value != (*adapter)->ip.value) { // INADDR_ANY will reference all interfaces, but if it's not that we have to be aligned with what the adapter is assigned with
            errno = ENETUNREACH;
            return -1;
        }

        if ((remote.value & (*adapter)->subnetmask.value) == ((*adapter)->ip.value & (*adapter)->subnetmask.value)) {
            islocal = true; // this is us!!!
        }
    } else {
        VECTOR_FOR_EACH(&net_adapters, it,
            struct net_adapter *itadapter = *it;

            if (local.value != INADDR_ANY && itadapter->ip.value != local.value) {
                continue; // is not us
            }

            if ((remote.value & itadapter->subnetmask.value) == (itadapter->ip.value & itadapter->subnetmask.value)) {
                islocal = true; // this is us!!!
                *adapter = itadapter; // set the adapter (to receive based on route)
            } else if (!islocal && itadapter->gateway.value != 0) {
                *adapter = itadapter;
            }
        );
    }

    if (!(*adapter)) {
        debug_print(0, "net: Route failed to find any adapters %d.%d.%d.%d->%d.%d.%d.%d\n", NET_PRINTIP(local), NET_PRINTIP(remote));
        errno = ENETUNREACH;
        return -1;
    }

    if (islocal) {
        int status = net_lookup(*adapter, remote, mac);
        if (status != 0) {
            return status;
        }
    } else {
        int status = net_lookup(*adapter, (*adapter)->gateway, mac); // since we set the MAC to the gateway all packets will be directed there, but on the IPv4 layer they will be forwarded by the router
        if (status != 0) {
            return status;
        }
    }

    return 0;
}

static void net_onicmp(struct net_adapter *adapter, struct net_inetheader *inetheader, size_t length) {
    if (length < sizeof(struct net_icmpheader)) {
        debug_print(0, "net: Discarded [too] short ICMP packet (len: %d)\n", length);
        return;
    }

    struct net_icmpheader *header = (struct net_icmpheader *)inetheader->data;
    // XXX: Checksum validation
    debug_print(0, "net: Received ICMP packet, type: %d, code: %d\n", header->type, header->code);
    if (header->type == 8) { // echo request
        uint8_t *buffer = alloc(sizeof(struct net_etherframe) + sizeof(struct net_inetheader) + length); // since ICMP is variable size
        struct net_etherframe *frame = (struct net_etherframe *)buffer;
        frame->type.value = __builtin_bswap16(NET_ETHPROTOIPV4); // IPv4
        frame->src = adapter->mac;

        if (inetheader->src.value == INADDR_BROADCAST) {
            frame->dest = (struct net_macaddr) { .mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };
        } else {
            ssize_t status = net_route(&adapter, adapter->ip, inetheader->src, &frame->dest);
            if (status != 0) {
                return;
            }
        }

        struct net_icmpheader *icmpreply = alloc(length);
        icmpreply->type = 0; // echo reply
        icmpreply->code = 0;
        icmpreply->csum.value = 0;
        memcpy(icmpreply->data, header->data, length - sizeof(struct net_icmpheader));
        icmpreply->csum = net_checksum(icmpreply, length);

        debug_print(0, "net: Sending ICMP ping reply to %d.%d.%d.%d (%02x:%02x:%02x:%02x:%02x:%02x)\n", NET_PRINTIP(inetheader->src), NET_PRINTMAC(frame->dest));
        net_sendinet(adapter, adapter->ip, inetheader->src, NET_IPPROTOICMP, icmpreply, length);
    }
}

static void net_oninet(struct net_adapter *adapter, void *data, size_t length) {
    if (length < sizeof(struct net_inetheader)) {
        debug_print(0, "net: Discarded [too] short IPv4 packet (len: %d)\n", length);
        return;
    }

    struct net_inetheader *header = (struct net_inetheader *)data;

    if (header->version != 4) {
        debug_print(0, "net: Invalid version on IPv4 packet (ver: %d)\n", header->version);
        return;
    }

    be_uint16_t csum = header->csum;

    header->csum.value = 0; // exclude checksum from calculation
    if (csum.value != net_checksum(data, sizeof(struct net_inetheader)).value) {
        debug_print(0, "net: Invalid checksum on IPv4 packet\n");
        return;
    }

    switch (header->protocol) {
        case NET_IPPROTOICMP: // ICMP
            net_onicmp(adapter, header, length - sizeof(struct net_inetheader));
            break;
        case NET_IPPROTOTCP: // TCP
            break;
        case NET_IPPROTOUDP: // UDP
            void net_onudp(struct net_adapter *, struct net_inetheader *, size_t);
            net_onudp(adapter, header, length - sizeof(struct net_inetheader));
            break;
    }
}

static void net_onarp(struct net_adapter *adapter, void *data, size_t length) {
    if (length < sizeof(struct net_arpheader)) {
        debug_print(0, "net: Discarded [too] short ARP packet (len: %d)\n", length);
        return;
    }

    struct net_arpheader *header = (struct net_arpheader *)data;

    if (__builtin_bswap16(header->opcode.value) == 0x01) { // request
        struct net_adapter *arpadapter = net_findadapterbyip(header->destpr);

        // XXX: DHCP client/Static config so we can stop pretending to be every single device on the network

        if (arpadapter) {
            debug_print(0, "net: ARP request for %d.%d.%d.%d from %d.%d.%d.%d, it is local adapter %s with %02x:%02x:%02x:%02x:%02x:%02x\n", NET_PRINTIP(header->destpr), NET_PRINTIP(header->srcpr), arpadapter->ifname, NET_PRINTMAC(arpadapter->mac));

            uint8_t *buffer = alloc(sizeof(struct net_etherframe) + sizeof(struct net_arpheader));
            struct net_etherframe *ethframe = (struct net_etherframe *)buffer;
            ethframe->type.value = __builtin_bswap16(NET_ETHPROTOARP); // ARP
            ethframe->src = arpadapter->mac;
            ethframe->dest = header->srchw;

            struct net_arpheader *reply = (struct net_arpheader *)ethframe->data;

            reply->desthw = header->srchw;
            reply->srchw = arpadapter->mac;
            reply->hwlen = 6;
            reply->hwtype.value = __builtin_bswap16(arpadapter->type);

            reply->destpr = header->srcpr;
            reply->srcpr = arpadapter->ip;
            reply->plen = 4;
            reply->prtype.value = __builtin_bswap16(NET_ETHPROTOIPV4);

            reply->opcode.value = __builtin_bswap16(2); // reply

            arpadapter->txpacket(arpadapter, buffer, sizeof(struct net_etherframe) + sizeof(struct net_arpheader)); // reply should come from the adapter in question rather than the one that picked up the packet
            free(buffer);
        }
    }

    struct net_inethwpair *pair = alloc(sizeof(struct net_inethwpair));
    pair->inet = header->srcpr;
    pair->hw = header->srchw;
    spinlock_acquire(&adapter->addrcachelock);
    event_trigger(&adapter->addrcacheupdate, false);
    VECTOR_PUSH_BACK(&adapter->addrcache, pair); // cache the ip-hw correlation from this ARP packet for later use (reduces the need for ip lookups)
    spinlock_release(&adapter->addrcachelock);
}

static noreturn void net_ifhandler(struct net_adapter *adapter) {
    debug_print(0, "net: Interface thread initialised on %s\n", adapter->ifname);

    for (;;) {
        struct event *events[] = { &adapter->packetevent };
        event_await(events, 1, true);

        spinlock_acquire(&adapter->cachelock); // lock our usage of the cache to prevent changing the contents of the cache between reads

        struct net_packet *packet = VECTOR_ITEM(&adapter->cache, 0); // grab latest

        struct net_etherframe *ethframe = NULL; // ethernet frame (in case of ethernet)
        if (adapter->type & NET_ADAPTERETH) { // ethernet
            ethframe = (struct net_etherframe *)packet->data;
            debug_print(0, "net: Ethernet packet of type %x sent to %02x:%02x:%02x:%02x:%02x:%02x from %02x:%02x:%02x:%02x:%02x:%02x on %s\n", __builtin_bswap16(ethframe->type.value), NET_PRINTMAC(ethframe->dest), NET_PRINTMAC(ethframe->src), adapter->ifname);
        }
        VECTOR_REMOVE(&adapter->cache, 0); // remove from cache to give us more space

        spinlock_release(&adapter->cachelock);

        if (adapter->type & NET_ADAPTERETH) { // ethernet
            switch (__builtin_bswap16(ethframe->type.value)) {
                case NET_ETHPROTOIPV4: // IPv4
                    net_oninet(adapter, ethframe->data, packet->len - sizeof(struct net_etherframe));
                    free(packet);
                    break;
                case NET_ETHPROTOARP: // ARP
                    net_onarp(adapter, ethframe->data, packet->len - sizeof(struct net_etherframe));
                    free(packet);
                    break;
            }
        }
    }
}

// Socket ioctl() for inet sockets to call upon their adapter
int net_sockioctl(struct resource *_this, struct f_description *description, uint64_t request, uint64_t arg) {
    struct inetsocket *this = (struct inetsocket *)_this;

    if (this->adapter) { // only if the adapter exists will this be passed onwards (interface binding explicitly/binded directly to an adapter)
        return net_ifioctl((struct resource *)this->adapter, description, request, arg); // pass all interface ioctls to if ioctl
    }
    return resource_default_ioctl(_this, description, request, arg);
}


// Direct ioctl() on adapter (from something like the device or via the socket)
int net_ifioctl(struct resource *_this, struct f_description *description, uint64_t request, uint64_t arg) {
    struct net_adapter *this = (struct net_adapter *)_this;

    switch (request) {
        case SIOCADDRT: {
            return 0;
        }
        case SIOCGIFNAME: {
            strcpy(((struct ifreq *)arg)->ifr_name, this->ifname);
            return 0;
        }
        case SIOCGIFADDR: {
            struct sockaddr_in *inaddr = (struct sockaddr_in *)&((struct ifreq *)arg)->ifr_addr;
            inaddr->sin_family = PF_INET;
            inaddr->sin_addr.s_addr = this->ip.value;
            return 0;
        }
        case SIOCSIFADDR: {
            struct sockaddr_in *inaddr = (struct sockaddr_in *)&((struct ifreq *)arg)->ifr_addr;
            if (inaddr->sin_family != PF_INET) {
                errno = EPROTONOSUPPORT;
                return -1;
            }

            this->ip.value = inaddr->sin_addr.s_addr;
            return 0;
        }
        case SIOCGIFNETMASK: {
            struct sockaddr_in *inaddr = (struct sockaddr_in *)&((struct ifreq *)arg)->ifr_netmask;
            inaddr->sin_family = PF_INET;
            inaddr->sin_addr.s_addr = this->subnetmask.value;
            return 0;
        }
        case SIOCSIFNETMASK: {
            struct sockaddr_in *inaddr = (struct sockaddr_in *)&((struct ifreq *)arg)->ifr_netmask;
            
            if (inaddr->sin_family != PF_INET) {
                errno = EPROTONOSUPPORT;
                return -1;
            }

            this->subnetmask.value = inaddr->sin_addr.s_addr;
            return 0;
        }
        case SIOCGIFHWADDR: {
            memcpy(((struct ifreq *)arg)->ifr_hwaddr.sa_data, this->mac.mac, sizeof(struct net_macaddr));
            return 0;
        }
        case SIOCGIFINDEX: {
            ((struct ifreq *)arg)->ifr_ifindex = this->index;
            return 0;
        }
    };

    return resource_default_ioctl(_this, description, request, arg);
}

void net_register(struct net_adapter *adapter) { 
    adapter->ifname = alloc(32);

    if (adapter->type & NET_ADAPTERLO) {
        strcpy(adapter->ifname, "lo");
    } else if (adapter->type & NET_ADAPTERETH) {
        snprintf(adapter->ifname, 32, "eth%d", net_ethcount++);
    }

    adapter->mtu = 1500; // this can be changed!
    adapter->index = net_adapters.length - 1; 

    adapter->addrcache = (typeof(adapter->addrcache))VECTOR_INIT;
    adapter->cache = (typeof(adapter->cache))VECTOR_INIT;
    adapter->cachelock = (spinlock_t)SPINLOCK_INIT;
    adapter->addrcachelock = (spinlock_t)SPINLOCK_INIT;

    VECTOR_PUSH_BACK(&net_adapters, adapter);

    sched_new_kernel_thread(net_ifhandler, adapter, true);
}

void net_init(void) {
    net_portbitmap = alloc(NET_PORTRANGEEND - NET_PORTRANGESTART);
    loopback_init();
}
