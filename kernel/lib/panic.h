#ifndef _LIB__PANIC_H
#define _LIB__PANIC_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <sys/cpu.h>

noreturn void panic(struct cpu_ctx *ctx, bool trace, const char *fmt, ...);

#endif
