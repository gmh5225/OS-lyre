#ifndef _FS__DEVTMPFS_H
#define _FS__DEVTMPFS_H

#include <stdbool.h>
#include <lib/resource.h>

void devtmpfs_init(void);
bool devtmpfs_add_device(struct resource *device, const char *name);

#endif
