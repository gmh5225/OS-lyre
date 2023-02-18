#ifndef _DEV__NET__NET_H
#define _DEV__NET__NET_H

#include <lib/lock.h>
#include <lib/resource.h>
#include <lib/vector.h>
#include <stdint.h>

#define NET_PORTRANGESTART 49152
#define NET_PORTRANGEEND UINT16_MAX

#define NET_ETHPROTOIPV4 0x800
#define NET_ETHPROTOARP 0x806

#define NET_IPPROTOICMP 0x01
#define NET_IPPROTOTCP 0x06
#define NET_IPPROTOUDP 0x11

typedef struct {
    union {
        uint16_t value; // big endian representation
        struct {
            uint8_t hi;
            uint8_t lo;
        } __attribute__((packed)); // pack together tightly
    };
} be_uint16_t;

typedef struct {
    union {
        uint32_t value;
        struct {
            union {
                uint16_t high;
                struct {
                    uint8_t hhi;
                    uint8_t hlo;
                };
            };
            union {
                uint16_t low;
                struct {
                    uint8_t lhi;
                    uint8_t llo;
                };
            };
        };
    };
} be_uint32_t;

struct net_inetaddr {
    union {
        struct {
            uint8_t data[4]; // encapsulate so union shares all this memory instead of individual elements
        };
        uint32_t value;
    };
};

struct net_icmpheader {
    uint8_t type;
    uint8_t code;
    be_uint16_t csum;
    uint8_t data[];
} __attribute__((packed));

struct net_inetheader {
    uint8_t ihl : 4;
    uint8_t version : 4;
    uint8_t ecn : 2;
    uint8_t dscp : 6;
    be_uint16_t len;
    be_uint16_t id;
    uint8_t flags : 3;
    uint16_t fragoff : 13;
    uint8_t ttl;
    uint8_t protocol;
    be_uint16_t csum;
    struct net_inetaddr src;
    struct net_inetaddr dest;
    uint8_t data[];
} __attribute__((packed));

struct net_macaddr {
    uint8_t mac[6];
} __attribute__((packed));

struct net_etherframe {
    struct net_macaddr dest;
    struct net_macaddr src;
    be_uint16_t type;
    uint8_t data[]; // data needs to be in a dynamic representation
} __attribute__((packed));

struct net_arpheader {
    be_uint16_t hwtype;
    be_uint16_t prtype;
    uint8_t hwlen;
    uint8_t plen;
    be_uint16_t opcode;
    struct net_macaddr srchw;
    struct net_inetaddr srcpr;
    struct net_macaddr desthw;
    struct net_inetaddr destpr;
} __attribute__((packed));

enum {
    NET_TCPFIN = 0x001,
    NET_TCPSYN = 0x002,
    NET_TCPRST = 0x004,
    NET_TCPPSH = 0x008,
    NET_TCPACK = 0x010,
    NET_TCPURG = 0x020,
    NET_TCPECE = 0x040,
    NET_TCPCWR = 0x080,
    NET_TCPNS = 0x100,
    NET_TCPMASK = 0x1ff
};

struct net_tcpflags {
    union {
        be_uint16_t flags;
        struct { // since we need the data offset we may as well include these flags
            uint8_t ns : 1;
            uint8_t rsvd : 3;
            uint8_t doff : 4;
            uint8_t fin : 1;
            uint8_t syn : 1;
            uint8_t rst : 1;
            uint8_t psh : 1;
            uint8_t ack : 1;
            uint8_t urg : 1;
            uint8_t ece : 1;
            uint8_t cwr : 1;
        };
    };
};

struct net_tcpheader {
    be_uint16_t srcport;
    be_uint16_t destport;
    be_uint32_t sequence;
    be_uint32_t acknumber;
    struct net_tcpflags; 
    be_uint16_t winsize;
    be_uint16_t csum;
    be_uint16_t urgent;
} __attribute__((packed));

struct net_udpheader {
    be_uint16_t srcport;
    be_uint16_t destport;
    be_uint16_t length;
    be_uint16_t csum;
    uint8_t data[];
} __attribute__((packed));

struct net_packet { // high-level kernel interface for packets
    size_t len;
    uint8_t *data;
};

struct net_inethwpair {
    struct net_inetaddr inet;
    struct net_macaddr hw;
};

struct net_adapter {
    struct resource;

    bool linkstate;
    struct net_macaddr mac;
    struct net_inetaddr ip;
    struct net_inetaddr gateway;
    struct net_inetaddr subnetmask;
    uint16_t ipframe;
    size_t index;
    size_t mtu;
    VECTOR_TYPE(struct net_inethwpair *) addrcache; // keep a record (per adapter as they may be connected to different networks) of IP-to-MAC records (from the ARP cache)
    struct event addrcacheupdate;
    spinlock_t addrcachelock;
    VECTOR_TYPE(struct net_packet *) cache;
    spinlock_t cachelock; // not the same as the char device lock
    char *ifname;
    uint8_t type;
    struct event packetevent; // signal for packet arrival

    spinlock_t socklock; // lock on socket
    VECTOR_TYPE(struct socket *) boundsocks;

    void (*txpacket)(struct net_adapter *adapter, const void *data, size_t length);
};

#define NET_IP(a, b, c, d) (((uint32_t)d << 24) | ((uint32_t)c << 16) | ((uint32_t)b << 8) | ((uint32_t)a << 0))
#define NET_IPSTRUCT(a) ({ (struct net_inetaddr) { .value = (a) }; })
#define NET_PRINTIP(a) (a).data[0], (a).data[1], (a).data[2], (a).data[3]
#define NET_MACSTRUCT(a, b, c, d, e, f) ({ (struct net_macaddr) { .mac = { a, b, c, d, e, f } }; })
#define NET_PRINTMAC(a) (a).mac[0], (a).mac[1], (a).mac[2], (a).mac[3], (a).mac[4], (a).mac[5]

enum {
    NET_ADAPTERETH = (1 << 0),
    NET_ADAPTERLO = (1 << 1)
};

int net_sockioctl(struct resource *_this, struct f_description *description, uint64_t request, uint64_t arg);
int net_ifioctl(struct resource *_this, struct f_description *description, uint64_t request, uint64_t arg);

be_uint16_t net_checksum(void *data, size_t length);
ssize_t net_sendinet(struct net_adapter *adapter, struct net_inetaddr src, struct net_inetaddr dest, uint8_t protocol, void *data, size_t length);
void net_bindsocket(struct net_adapter *adapter, struct socket *sock);
void net_unbindsocket(struct net_adapter *adapter, struct socket *sock);
void net_unbindall(struct net_adapter *adapter);
uint16_t net_allocport(void);
void net_releaseport(uint16_t port);
struct net_adapter *net_findadapterbyip(struct net_inetaddr addr);
ssize_t net_lookup(struct net_adapter *adapter, struct net_inetaddr ip, struct net_macaddr *mac);
ssize_t net_route(struct net_adapter **adapter, struct net_inetaddr local, struct net_inetaddr remote, struct net_macaddr *mac);
void net_register(struct net_adapter *adapter);
void net_init(void);

void loopback_init(void);

#endif
