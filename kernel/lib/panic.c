#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <dev/lapic.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <sys/cpu.h>
#include <sys/idt.h>

noreturn void panic(struct cpu_ctx *ctx, const char *fmt, ...) {
    interrupt_toggle(false);

    static spinlock_t panic_lock = SPINLOCK_INIT;

    spinlock_acquire(&panic_lock);

    lapic_send_ipi(0, idt_panic_ipi_vector | 0b10 << 18);

    // Force unlock the print lock
    print_lock = SPINLOCK_INIT;

    kernel_print("\n\n*** LYRE PANIC ***\n\n");
    kernel_print("The Lyre kernel panicked with the following message:\n  ");

    va_list args;
    va_start(args, fmt);

    kernel_vprint(fmt, args);
    kernel_print("\n\n");

    if (ctx == NULL) {
        goto halt;
    }

    uint64_t cr2 = read_cr2();
    uint64_t cr3 = read_cr3();
    kernel_print("CPU context at panic:\n");
    kernel_print("  RAX=%016lx  RBX=%016lx\n"
                 "  RCX=%016lx  RDX=%016lx\n"
                 "  RSI=%016lx  RDI=%016lx\n"
                 "  RBP=%016lx  RSP=%016lx\n"
                 "  R08=%016lx  R09=%016lx\n"
                 "  R10=%016lx  R11=%016lx\n"
                 "  R12=%016lx  R13=%016lx\n"
                 "  R14=%016lx  R15=%016lx\n"
                 "  RIP=%016lx  RFLAGS=%08lx\n"
                 "  CS=%04lx DS=%04lx ES=%04lx SS=%04lx\n"
                 "  CR2=%016lx  CR3=%016lx\n"
                 "  ERR=%016lx",
                 ctx->rax, ctx->rbx, ctx->rcx, ctx->rdx,
                 ctx->rsi, ctx->rdi, ctx->rbp, ctx->rsp,
                 ctx->r8, ctx->r9, ctx->r10, ctx->r11,
                 ctx->r12, ctx->r13, ctx->r14, ctx->r15,
                 ctx->rip, ctx->rflags,
                 ctx->cs, ctx->ds, ctx->es, ctx->ss,
                 cr2, cr3, ctx->err);

    kernel_print("\n\n");

halt:
    kernel_print("System halted.");

    for (;;) {
        halt();
    }
}
