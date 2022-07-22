#include <stdbool.h>
#include <sched/sched.h>

void sched_await(void) {
    // STUB
    asm("cli; hlt");
}

bool sched_ready(void) {
    // STUB
    return false;
}
