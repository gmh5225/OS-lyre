#include <dev/net/net.h>
#include <fs/devtmpfs.h>
#include <lib/print.h>
#include <sched/sched.h>

static void loopback_transmitpacket(struct net_adapter *device, const void *data, size_t length) {
    struct net_packet *packet = alloc(sizeof(struct net_packet));
    packet->len = length;
    packet->data = alloc(length);
    memcpy(packet->data, data, length);

    // immediately dump this packet back on itself
    spinlock_acquire(&device->cachelock);
    VECTOR_PUSH_BACK(&device->cache, packet);
    spinlock_release(&device->cachelock);
    event_trigger(&device->packetevent, false);
}

void loopback_init(void) {
    struct net_adapter *dev = resource_create(sizeof(struct net_adapter));

    dev->can_mmap = false;
    dev->stat.st_mode = 0666 | S_IFCHR;
    dev->stat.st_rdev = resource_create_dev_id();
    dev->ioctl = net_ifioctl;

    dev->txpacket = loopback_transmitpacket;

    dev->type = NET_ADAPTERETH | NET_ADAPTERLO; // Ethernet-based Loopback device
    net_register((struct net_adapter *)dev);

    dev->cachelock = (spinlock_t)SPINLOCK_INIT;

    dev->ip = NET_IPSTRUCT(NET_IP(127, 0, 0, 1));
    dev->subnetmask = NET_IPSTRUCT(NET_IP(255, 0, 0, 0));

    devtmpfs_add_device((struct resource *)dev, dev->ifname);
}
