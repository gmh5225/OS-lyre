#include <stdint.h>
#include <stddef.h>
#include <dev/char/serial.h>
#include <dev/lapic.h>
#include <lib/print.h>
#include <mm/pmm.h>
#include <mm/slab.h>
#include <mm/vmm.h>
#include <sys/gdt.h>
#include <sys/idt.h>
#include <sys/except.h>
#include <fs/vfs/vfs.h>
#include <limine.h>
#include <fs/initramfs.h>
#include <fs/tmpfs.h>
#include <fs/devtmpfs.h>
#include <sched/sched.h>

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent.

static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0
};

static volatile struct limine_bootloader_info_request boot_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};

static void done(void) {
    for (;;) {
        __asm__("hlt");
    }
}

void kmain_thread(void);

// The following will be our kernel's entry point.
void _start(void) {
    serial_init();
    gdt_init();
    idt_init();
    except_init();
    pmm_init();
    slab_init();
    vmm_init();

    kernel_process = ALLOC(struct process);
    kernel_process->mmap_anon_base = 0x80000000000;
    kernel_process->pagemap = vmm_kernel_pagemap;

    cpu_init();

    sched_new_kernel_thread(kmain_thread, NULL, true);
    sched_await();
}

void kmain_thread(void) {
    vfs_init();
    tmpfs_init();
    devtmpfs_init();
    vfs_mount(vfs_root, NULL, "/", "tmpfs");
    vfs_create(vfs_root, "/dev", 0755 | S_IFDIR);
    vfs_mount(vfs_root, NULL, "/dev", "devtmpfs");
    initramfs_init();

    print("Hello, %s!\n", "world");

    if (boot_info_request.response) {
        print("Booted by %s %s\n", boot_info_request.response->name,
            boot_info_request.response->version);
    }

    // Ensure we got a terminal
    if (terminal_request.response == NULL
     || terminal_request.response->terminal_count < 1) {
        done();
    }

    // We should now be able to call the Limine terminal to print out
    // a simple "Hello World" to screen.
    struct limine_terminal *terminal = terminal_request.response->terminals[0];
    terminal_request.response->write(terminal, "Hello World", 11);

    // We're done, just hang...
    done();
}
