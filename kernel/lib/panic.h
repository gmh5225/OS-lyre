#ifndef _LIB__PANIC_H
#define _LIB__PANIC_H

#include <stdnoreturn.h>
#include <sys/cpu.h>

noreturn void panic(struct cpu_ctx *ctx, const char *fmt, ...);

#endif
