#ifndef _SYS__TIMER_H
#define _SYS__TIMER_H

#if defined (__x86_64__)

#include <dev/lapic.h>

static inline void sys_timer_oneshot(size_t us, void *function) {
    lapic_timer_oneshot(us, function);
}

#endif

#endif
