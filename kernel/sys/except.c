#include <stdint.h>
#include <stddef.h>
#include <sys/except.h>
#include <sys/idt.h>
#include <lib/misc.h>
#include <lib/print.h>

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

static void exception_handler(uint8_t vector, void *ctx) {
    // TODO this is a stub
    (void)ctx;

    print("Exception %s triggered\n", exceptions[vector]);
    for (;;);
}

void except_init(void) {
    for (size_t i = 0; i < SIZEOF_ARRAY(exceptions); i++) {
        isr[i] = exception_handler;
    }
}
