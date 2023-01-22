#ifndef _LIB__PRINT_H
#define _LIB__PRINT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/lock.h>

extern spinlock_t debug_print_lock;
extern bool debug_on;

size_t vsnprint(char *buffer, size_t size, const char *fmt, va_list args);
size_t snprint(char *buffer, size_t size, const char *fmt, ...);

void kernel_vprint(const char *fmt, va_list args);
void kernel_print(const char *fmt, ...);
void debug_vprint(size_t indent, const char *fmt, va_list args);
void debug_print(size_t indent, const char *fmt, ...);

#endif
