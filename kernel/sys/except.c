#include <stdint.h>
#include <stddef.h>
#include <mm/mmap.h>
#include <sys/except.h>
#include <sys/idt.h>
#include <lib/misc.h>
#include <lib/print.h>
#include <lib/panic.h>

static const char *exceptions[] = {
    "Division exception",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "???",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "???",
    "x87 exception",
    "Alignment check",
    "Machine check",
    "SIMD exception",
    "Virtualisation"
};

static void exception_handler(uint8_t vector, struct cpu_ctx *ctx) {
    if (vector == 0xe && mmap_handle_pf(ctx)) {
        return;
    }

    panic(ctx, true, "Exception %s triggered", exceptions[vector]);

    __builtin_unreachable();
}

void except_init(void) {
    for (size_t i = 0; i < SIZEOF_ARRAY(exceptions); i++) {
        isr[i] = exception_handler;
    }

    //idt_set_ist(0xe, 2); // #PF uses IST 2
    idt_set_ist(0x6, 3); // #UD uses IST 3
}
