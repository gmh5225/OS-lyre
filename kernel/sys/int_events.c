#include <stdint.h>
#include <sys/int_events.h>
#include <sys/idt.h>
#include <sys/cpu.h>
#include <lib/event.h>
#include <dev/lapic.h>

struct event int_events[256];

static void int_events_handler(uint8_t vector, struct cpu_ctx *ctx) {
    (void)ctx;
    lapic_eoi();
    event_trigger(&int_events[vector], false);
}

void int_events_init(void) {
    for (size_t i = 32; i < 0xef; i++) {
        isr[i] = int_events_handler;
    }
}
