#ifndef _SYS__INT_EVENTS_H
#define _SYS__INT_EVENTS_H

#include <lib/event.h>

extern struct event int_events[256];

void int_events_init(void);

#endif
