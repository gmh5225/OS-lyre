#ifndef _LIB__ERRNO_H
#define _LIB__ERRNO_H

#include <abi-bits/errno.h>
#include <sched/proc.h>

#define errno (sched_current_thread()->errno)

#endif
