#include <stdbool.h>
#include <fs/vfs/vfs.h>
#include <lib/alloc.h>
#include <lib/hashmap.h>
#include <lib/lock.h>
#include <lib/errno.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <sched/proc.h>
#include <bits/posix/stat.h>
#include <abi-bits/fcntl.h>

static spinlock_t vfs_lock = SPINLOCK_INIT;

struct vfs_node *vfs_create_node(struct vfs_filesystem *fs, struct vfs_node *parent,
                                 const char *name, bool dir) {
    struct vfs_node *node = ALLOC(struct vfs_node);

    node->name = alloc(strlen(name) + 1);
    strcpy(node->name, name);

    node->parent = parent;
    node->filesystem = fs;

    if (dir) {
        node->children = (typeof(node->children))HASHMAP_INIT(256);
    }

    return node;
}

static void create_dotentries(struct vfs_node *node, struct vfs_node *parent) {
    struct vfs_node *dot = vfs_create_node(node->filesystem, node, ".", false);
    struct vfs_node *dotdot = vfs_create_node(node->filesystem, node, "..", false);

    dot->redir = node;
    dotdot->redir = parent;

    HASHMAP_SINSERT(&node->children, ".", dot);
    HASHMAP_SINSERT(&node->children, "..", dotdot);
}

static HASHMAP_TYPE(struct vfs_filesystem *) filesystems;

void vfs_add_filesystem(struct vfs_filesystem *fs, const char *identifier) {
    spinlock_acquire(&vfs_lock);

    HASHMAP_SINSERT(&filesystems, identifier, fs);

    spinlock_release(&vfs_lock);
}

struct vfs_node *vfs_root = NULL;

void vfs_init(void) {
    vfs_root = vfs_create_node(NULL, NULL, "", false);

    filesystems = (typeof(filesystems))HASHMAP_INIT(256);
}

struct path2node_res {
    struct vfs_node *target_parent;
    struct vfs_node *target;
    char *basename;
};

static struct vfs_node *reduce_node(struct vfs_node *node, bool follow_symlinks);

static struct path2node_res path2node(struct vfs_node *parent, const char *path) {
    if (path == NULL || strlen(path) == 0) {
        errno = ENOENT;
        return (struct path2node_res){NULL, NULL, NULL};
    }

    size_t path_len = strlen(path);

    size_t index = 0;
    struct vfs_node *current_node = reduce_node(parent, false);

    if (path[index] == '/') {
        current_node = reduce_node(vfs_root, false);
        while (path[index] == '/') {
            if (index == path_len - 1) {
                return (struct path2node_res){current_node, current_node, strdup("")};
            }
            index++;
        }
    }

    for (;;) {
        const char *elem = &path[index];
        size_t elem_len = 0;

        while (index < path_len && path[index] != '/') {
            elem_len++, index++;
        }

        while (index < path_len && path[index] == '/') {
            index++;
        }

        bool last = index == path_len;

        char *elem_str = alloc(elem_len + 1);
        memcpy(elem_str, elem, elem_len);

        current_node = reduce_node(current_node, false);

        struct vfs_node *new_node;
        if (!HASHMAP_SGET(&current_node->children, new_node, elem_str)) {
            errno = ENOENT;
            if (last) {
                return (struct path2node_res){current_node, NULL, elem_str};
            }
            return (struct path2node_res){NULL, NULL, NULL};
        }

        new_node = reduce_node(new_node, false);

        if (last) {
            return (struct path2node_res){current_node, new_node, elem_str};
        }

        current_node = new_node;

        if (S_ISLNK(current_node->resource->stat.st_mode)) {
            struct path2node_res r = path2node(current_node->parent, current_node->symlink_target);
            if (r.target == NULL) {
                return (struct path2node_res){NULL, NULL, NULL};
            }
            continue;
        }

        if (!S_ISDIR(current_node->resource->stat.st_mode)) {
            errno = ENOTDIR;
            return (struct path2node_res){NULL, NULL, NULL};
        }
    }

    errno = ENOENT;
    return (struct path2node_res){NULL, NULL, NULL};
}

static struct vfs_node *get_parent_dir(int dir_fdnum, const char *path) {
    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    // struct vfs_node *parent = NULL;

    if (*path == '/') {
        return vfs_root;
    } else if (dir_fdnum == AT_FDCWD) {
        return proc->cwd;
    }

    struct f_descriptor *fd = fd_from_fdnum(proc, dir_fdnum);
    if (fd == NULL) {
        return NULL;
    }

    struct f_description *description = fd->description;
    if (!S_ISDIR(description->res->stat.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }

    return description->node;
}

static struct vfs_node *reduce_node(struct vfs_node *node, bool follow_symlinks) {
    if (node->redir != NULL) {
        return reduce_node(node->redir, follow_symlinks);
    }
    if (node->mountpoint != NULL) {
        return reduce_node(node->mountpoint, follow_symlinks);
    }
    if (node->symlink_target != NULL && follow_symlinks == true) {
        struct path2node_res r = path2node(node->parent, node->symlink_target);
        if (r.target == NULL) {
            return NULL;
        }
        return reduce_node(r.target, follow_symlinks);
    }
    return node;
}

struct vfs_node *vfs_get_node(struct vfs_node *parent, const char *path, bool follow_links) {
    spinlock_acquire(&vfs_lock);

    struct vfs_node *ret = NULL;

    struct path2node_res r = path2node(parent, path);
    if (r.target == NULL) {
        goto cleanup;
    }

    if (follow_links) {
        ret = reduce_node(r.target, true);
        goto cleanup;
    }

    ret = r.target;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

bool vfs_mount(struct vfs_node *parent, const char *source, const char *target,
               const char *fs_name) {
    spinlock_acquire(&vfs_lock);

    bool ret = false;
    struct path2node_res r = {0};

    struct vfs_filesystem *fs;
    if (!HASHMAP_SGET(&filesystems, fs, fs_name)) {
        errno = ENODEV;
        goto cleanup;
    }

    struct vfs_node *source_node = NULL;
    if (source != NULL && strlen(source) != 0) {
        struct path2node_res rr = path2node(parent, source);
        source_node = rr.target;
        if (rr.basename != NULL) {
            free(rr.basename);
        }
        if (source_node == NULL) {
            goto cleanup;
        }
        if (!S_ISDIR(source_node->resource->stat.st_mode)) {
            errno = EISDIR;
            goto cleanup;
        }
    }

    r = path2node(parent, target);

    bool mounting_root = r.target == vfs_root;

    if (r.target == NULL) {
        goto cleanup;
    }

    if (!mounting_root && !S_ISDIR(r.target->resource->stat.st_mode)) {
        errno = EISDIR;
        goto cleanup;
    }

    struct vfs_node *mount_node = fs->mount(r.target_parent, r.basename, source_node);

    r.target->mountpoint = mount_node;

    create_dotentries(mount_node, r.target_parent);

    if (source != NULL && strlen(source) != 0) {
        print("vfs: Mounted `%s` on `%s` with filesystem `%s`\n", source, target, fs_name);
    } else {
        print("vfs: Mounted %s on `%s`\n", fs_name, target);
    }

    ret = true;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

struct vfs_node *vfs_symlink(struct vfs_node *parent, const char *dest,
                             const char *target) {
    spinlock_acquire(&vfs_lock);

    struct vfs_node *ret = NULL;

    struct path2node_res r = path2node(parent, target);

    if (r.target_parent == NULL) {
        goto cleanup;
    }

    if (r.target != NULL) {
        errno = EEXIST;
        goto cleanup;
    }

    struct vfs_filesystem *target_fs = r.target_parent->filesystem;
    struct vfs_node *target_node = target_fs->symlink(target_fs, r.target_parent, r.basename, dest);

    HASHMAP_SINSERT(&r.target_parent->children, r.basename, target_node);

    ret = target_node;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

struct vfs_node *vfs_create(struct vfs_node *parent, const char *name, int mode) {
    spinlock_acquire(&vfs_lock);

    struct vfs_node *ret = NULL;

    struct path2node_res r = path2node(parent, name);

    if (r.target_parent == NULL) {
        goto cleanup;
    }

    if (r.target != NULL) {
        errno = EEXIST;
        goto cleanup;
    }

    struct vfs_filesystem *target_fs = r.target_parent->filesystem;
    struct vfs_node *target_node = target_fs->create(target_fs, r.target_parent, r.basename, mode);

    HASHMAP_SINSERT(&r.target_parent->children, r.basename, target_node);

    if (S_ISDIR(target_node->resource->stat.st_mode)) {
        create_dotentries(target_node, r.target_parent);
    }

    ret = target_node;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

int syscall_openat(void *_, int dir_fdnum, const char *path, int flags, int mode) {
    (void)_;
    (void)mode;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(path) == 0) {
        errno = ENOENT;
        return -1;
    }

    struct vfs_node *parent = get_parent_dir(dir_fdnum, path);
    if (parent == NULL) {
        return -1;
    }

    int create_flags = flags & FILE_CREATION_FLAGS_MASK;
    int follow_links = (flags & O_NOFOLLOW) == 0;

    struct vfs_node *node = vfs_get_node(parent, path, follow_links);
    if (node == NULL && (create_flags & O_CREAT) != 0) {
        node = vfs_create(parent, path, 0644 | S_IFREG);
    }

    if (node == NULL) {
        return -1;
    }

    if (S_ISLNK(node->resource->stat.st_mode)) {
        errno = ELOOP;
        return -1;
    }

    node = reduce_node(node, true);
    if (node == NULL) {
        return -1;
    }

    if (!S_ISDIR(node->resource->stat.st_mode) && (flags & O_DIRECTORY) != 0) {
        errno = ENOTDIR;
        return -1;
    }

    struct f_descriptor *fd = fd_create_from_resource(node->resource, flags);
    if (fd == NULL) {
        return -1;
    }

    fd->description->node = node;
    return fdnum_create_from_fd(proc, fd, 0, false);
}

int syscall_stat(void *_, int dir_fdnum, const char *path, int flags, struct stat *stat_buf) {
    (void)_;

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct stat *stat_src = NULL;

    if (stat_buf == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(path) == 0) {
        if ((flags & AT_EMPTY_PATH) == 0) {
            errno = ENOENT;
            return -1;
        }

        if (dir_fdnum == AT_FDCWD) {
            stat_src = &proc->cwd->resource->stat;
        } else {
            struct f_descriptor *fd = fd_from_fdnum(proc, dir_fdnum);
            if (fd == NULL) {
                return -1;
            }

            stat_src = &fd->description->res->stat;
        }
    } else {
        struct vfs_node *parent = get_parent_dir(dir_fdnum, path);
        if (parent == NULL) {
            return -1;
        }

        struct vfs_node *node = vfs_get_node(parent, path, (flags & AT_SYMLINK_NOFOLLOW) == 0);
        if (node == NULL) {
            return -1;
        }

        stat_src = &node->resource->stat;
    }

    memcpy(stat_buf, stat_src, sizeof(struct stat));
    return 0;
}
