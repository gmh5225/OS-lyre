#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <lib/resource.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <mm/vmm.h>
#include <dev/char/console.h>
#include <fs/devtmpfs.h>
#include <sys/cpu.h>

static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0
};

struct tty {
    struct resource;
    struct limine_terminal *terminal;
};

static ssize_t tty_read(struct resource *_this, void *buf, off_t offset, size_t count) {
    (void)_this; (void)buf; (void)offset;

    return count;
}

static ssize_t tty_write(struct resource *_this, const void *buf, off_t offset, size_t count) {
    (void)offset;

    struct tty *this = (struct tty *)_this;

    char *local = alloc(count);
    memcpy(local, buf, count);

    uint64_t cr3 = read_cr3();
    if (cr3 != (uint64_t)vmm_kernel_pagemap->top_level - VMM_HIGHER_HALF) {
        vmm_switch_to(vmm_kernel_pagemap);
    }

    terminal_request.response->write(this->terminal, local, count);

    if (cr3 != (uint64_t)vmm_kernel_pagemap->top_level - VMM_HIGHER_HALF) {
        write_cr3(cr3);
    }

    free(local);

    return count;
}

void console_init(void) {
    struct limine_terminal_response *terminal_resp = terminal_request.response;

    if (terminal_resp == NULL || terminal_resp->terminal_count < 1) {
        // TODO: Should not be a hard requirement..?
        panic(NULL, true, "Limine terminal is not available");
    }

    struct tty *terminal_dev = resource_create(sizeof(struct tty));

    terminal_dev->read = tty_read;
    terminal_dev->write = tty_write;
    terminal_dev->terminal = terminal_resp->terminals[0];

    devtmpfs_add_device((struct resource *)terminal_dev, "console");
}
