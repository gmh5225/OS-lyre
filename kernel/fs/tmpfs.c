#include <stddef.h>
#include <stdint.h>
#include <fs/tmpfs.h>
#include <fs/vfs/vfs.h>
#include <lib/alloc.h>
#include <lib/errno.h>
#include <lib/lock.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <bits/posix/stat.h>

struct tmpfs_resource {
    struct resource;

    void *data;
    size_t capacity;
};

struct tmpfs {
    struct vfs_filesystem;

    uint64_t dev_id;
    uint64_t inode_counter;
};

static inline struct vfs_filesystem *tmpfs_instantiate(void);

struct vfs_node *tmpfs_mount(struct vfs_node *parent, const char *name, struct vfs_node *source) {
    (void)source;

    struct vfs_filesystem *new_fs = tmpfs_instantiate();
    struct vfs_node *ret = new_fs->create(new_fs, parent, name, 0644 | S_IFDIR);
    return ret;
}

struct vfs_node *tmpfs_create(struct vfs_filesystem *_this, struct vfs_node *parent,
                              const char *name, int mode) {
    struct tmpfs *this = (struct tmpfs *)_this;
    struct vfs_node *new_node = NULL;
    struct tmpfs_resource *resource = NULL;

    new_node = vfs_create_node(_this, parent, name, S_ISDIR(mode));
    if (new_node == NULL) {
        goto fail;
    }

    resource = ALLOC(struct tmpfs_resource);
    if (resource == NULL) {
        goto fail;
    }

    if (S_ISREG(mode)) {
        resource->capacity = 4096;
        resource->data = alloc(resource->capacity);
        // TODO: Finish up the resource API
        // resource->can_mmap = true;
    }

    resource->refcount = 1;
    resource->stat.st_size = 0;
    resource->stat.st_blocks = 0;
    resource->stat.st_blksize = 512;
    resource->stat.st_dev = this->dev_id;
    resource->stat.st_ino = this->inode_counter++;
    resource->stat.st_mode = mode;
    resource->stat.st_nlink = 1;

    // TODO: Port time stuff in
	// resource->stat.st_atim = realtime_clock;
	// resource->stat.st_ctim = realtime_clock;
	// resource->stat.st_mtim = realtime_clock;

	new_node->resource = (struct resource *)resource;
    return new_node;

fail:
    if (new_node != NULL) {
        free(new_node); // TODO: Use vfs_destroy_node
    }
    if (resource != NULL) {
        free(resource);
    }

    return NULL;
}

struct vfs_node *tmpfs_symlink(struct vfs_filesystem *_this, struct vfs_node *parent,
                               const char *name, const char *target) {
    struct tmpfs *this = (struct tmpfs *)_this;
    struct vfs_node *new_node = NULL;
    struct tmpfs_resource *resource = NULL;

    new_node = vfs_create_node(_this, parent, name, false);
    if (new_node == NULL) {
        goto fail;
    }

    resource = ALLOC(struct tmpfs_resource);
    if (resource == NULL) {
        goto fail;
    }

    resource->refcount = 1;
    resource->stat.st_size = strlen(target);
    resource->stat.st_blocks = 0;
    resource->stat.st_blksize = 512;
    resource->stat.st_dev = this->dev_id;
    resource->stat.st_ino = this->inode_counter++;
    resource->stat.st_mode = 0777 | S_IFLNK;
    resource->stat.st_nlink = 1;

    // TODO: Port time stuff in
	// resource->stat.st_atim = realtime_clock;
	// resource->stat.st_ctim = realtime_clock;
	// resource->stat.st_mtim = realtime_clock;

	new_node->resource = (struct resource *)resource;
    new_node->symlink_target = strdup(name);
    return new_node;

fail:
    if (new_node != NULL) {
        free(new_node); // TODO: Use vfs_destroy_node
    }
    if (resource != NULL) {
        free(resource);
    }

    return NULL;
}

struct vfs_node *tmpfs_link(struct vfs_filesystem *_this, struct vfs_node *parent,
                            const char *name, struct vfs_node *node) {
    if (S_ISDIR(node->resource->stat.st_mode)) {
        errno = EISDIR;
        return NULL;
    }

    struct vfs_node *new_node = vfs_create_node(_this, parent, name, false);
    if (new_node == NULL) {
        return NULL;
    }

    new_node->resource = node->resource;
    return new_node;
}

static inline struct vfs_filesystem *tmpfs_instantiate(void) {
    struct tmpfs *new_fs = ALLOC(struct tmpfs);
    if (new_fs == NULL) {
        return NULL;
    }

    new_fs->mount = tmpfs_mount;
    new_fs->create = tmpfs_create;
    new_fs->symlink = tmpfs_symlink;
    new_fs->link = tmpfs_link;

    return (struct vfs_filesystem *)new_fs;
}

void tmpfs_init(void) {
    struct vfs_filesystem *tmpfs = tmpfs_instantiate();
    if (tmpfs == NULL) {
        panic(NULL, "Failed to instantiate tmpfs");
    }

    vfs_add_filesystem(tmpfs, "tmpfs");
}
