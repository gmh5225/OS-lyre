#include <stdint.h>
#include <stddef.h>
#include <dev/char/serial.h>
#include <dev/char/console.h>
#include <dev/lapic.h>
#include <dev/ps2.h>
#include <lib/elf.h>
#include <lib/print.h>
#include <mm/pmm.h>
#include <mm/slab.h>
#include <mm/vmm.h>
#include <sys/gdt.h>
#include <sys/idt.h>
#include <sys/except.h>
#include <sys/int_events.h>
#include <fs/vfs/vfs.h>
#include <limine.h>
#include <fs/initramfs.h>
#include <fs/tmpfs.h>
#include <fs/devtmpfs.h>
#include <sched/sched.h>
#include <acpi/acpi.h>
#include <time/time.h>

void kmain_thread(void);

void *mock_thread = &mock_thread;

void _start(void) {
    serial_init();
    gdt_init();
    set_gs_base(&mock_thread);
    idt_init();
    except_init();
    int_events_init();
    pmm_init();
    slab_init();
    vmm_init();
    proc_init();

    kernel_process = sched_new_process(NULL, vmm_kernel_pagemap);

    cpu_init();
    acpi_init();
    time_init();

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
    ps2_init();
    console_init();
    initramfs_init();

    struct pagemap *bash_vm = vmm_new_pagemap();
    struct auxval bash_auxv, ld_auxv;
    const char *ld_path;

    struct vfs_node *bin_bash = vfs_get_node(vfs_root, "/bin/bash", true);
    elf_load(bash_vm, bin_bash->resource, 0x0, &bash_auxv, &ld_path);

    struct vfs_node *ld = vfs_get_node(vfs_root, ld_path, true);
    elf_load(bash_vm, ld->resource, 0x40000000, &ld_auxv, NULL);

    const char *argv[] = {"/bin/bash", "-l", NULL};
    const char *envp[] = {"USER=root", "HOME=/root", "TERM=linux", NULL};

    struct process *bash_proc = sched_new_process(NULL, bash_vm);
    struct vfs_node *dev_tty1 = vfs_get_node(vfs_root, "/dev/console", true);

    fdnum_create_from_resource(bash_proc, dev_tty1->resource, 0, 0, true);
    fdnum_create_from_resource(bash_proc, dev_tty1->resource, 0, 1, true);
    fdnum_create_from_resource(bash_proc, dev_tty1->resource, 0, 2, true);

    vfs_pathname(bin_bash, bash_proc->name, sizeof(bash_proc->name) - 1);
    sched_new_user_thread(bash_proc, (void *)ld_auxv.at_entry,
                          NULL, NULL, argv, envp, &bash_auxv, true);

    sched_dequeue_and_die();
}
