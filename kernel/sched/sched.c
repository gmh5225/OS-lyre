#include <stdbool.h>
#include <stdnoreturn.h>
#include <lib/print.h>
#include <sched/sched.h>
#include <sys/timer.h>
#include <sys/cpu.h>

struct process *kernel_process;

static bool ready = false;

static void sched_entry(int vector, struct cpu_ctx *ctx) {
    (void)vector; (void)ctx;
    print("sched_entry() reached\n");
    for (;;);
    __builtin_unreachable();
}

noreturn void sched_await(void) {
    interrupt_toggle(false);
    sys_timer_oneshot(20000, sched_entry);
    interrupt_toggle(true);
    for (;;);
    __builtin_unreachable();
}

bool sched_ready(void) {
    return ready;
}

void sched_init(void) {
    ready = true;
}
