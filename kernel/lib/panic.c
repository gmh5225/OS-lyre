#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <sys/cpu.h>

noreturn void panic(struct cpu_ctx *ctx, const char *fmt, ...) {
    // TODO replace with some abort IPI and whatnot
    asm volatile ("cli");

    print("\n\n*** LYRE PANIC ***\n\n");

    print("The Lyre kernel panicked with the following message:\n  ");

    va_list args;

    va_start(args, fmt);
    vprint(fmt, args);
    va_end(args);

    print("\n\n");

    if (ctx == NULL) {
        goto halt;
    }

    print("CPU context at panic:\n");
    print("  RAX=%lx  RBX=%lx\n"
          "  RCX=%lx  RDX=%lx\n"
          "  RSI=%lx  RDI=%lx\n"
          "  RBP=%lx  RSP=%lx\n"
          "  R08=%lx  R09=%lx\n"
          "  R10=%lx  R11=%lx\n"
          "  R12=%lx  R13=%lx\n"
          "  R14=%lx  R15=%lx\n"
          "  RIP=%lx  RFLAGS=%lx\n"
          "  CS=%lx DS=%lx ES=%lx SS=%lx\n"
          "  ERR=%lx",
          ctx->rax, ctx->rbx, ctx->rcx, ctx->rdx,
          ctx->rsi, ctx->rdi, ctx->rbp, ctx->rsp,
          ctx->r8, ctx->r9, ctx->r10, ctx->r11,
          ctx->r12, ctx->r13, ctx->r14, ctx->r15,
          ctx->rip, ctx->rflags,
          ctx->cs, ctx->ds, ctx->es, ctx->ss,
          ctx->err);

    print("\n\n");

halt:
    print("System halted.");

    for (;;) {
        asm volatile ("hlt");
    }
}
