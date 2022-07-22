#include <stdbool.h>
#include <sched/sched.h>

struct process *kernel_process;

void sched_await(void) {
    // STUB
    asm("cli; hlt");
}

bool sched_ready(void) {
    // STUB
    return false;
}

void sched_init(void) {

    // TODO the rest
}
