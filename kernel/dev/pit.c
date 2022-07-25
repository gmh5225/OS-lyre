#include <stdint.h>
#include <lib/print.h>
#include <dev/pit.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>
#include <sys/cpu.h>
#include <sys/idt.h>
#include <sys/port.h>
#include <time/time.h>

uint16_t pit_get_current_count(void) {
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40) << 8;
    return ((uint16_t)hi << 8) | lo;
}

void pit_set_reload_value(uint16_t new_count) {
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)new_count);
    outb(0x40, (uint8_t)(new_count >> 8));
}

void pit_set_frequency(uint64_t frequency) {
    uint64_t new_divisor = PIT_DIVIDEND / frequency;
    if (PIT_DIVIDEND % frequency > frequency / 2) {
        new_divisor++;
    }
    pit_set_reload_value((uint16_t)new_divisor);
}

extern void timer_handler(void);

static void pit_timer_handler(int vector, struct cpu_ctx *ctx) {
    (void)vector;
    (void)ctx;

    lapic_eoi();
    timer_handler();
}

void pit_init(void) {
    pit_set_frequency(TIMER_FREQ);

    uint8_t timer_vector = idt_allocate_vector();

    isr[timer_vector] = pit_timer_handler;
    io_apic_set_irq_redirect(lapic_get_id(), timer_vector, 0, true);
}
