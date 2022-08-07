#ifndef _LIB__PRINT_H
#define _LIB__PRINT_H

#include <stdarg.h>
#include <stddef.h>

size_t vsnprint(char *buffer, size_t size, const char *fmt, va_list args);
size_t snprint(char *buffer, size_t size, const char *fmt, ...);

void kernel_vprint(const char *fmt, va_list args);
void kernel_print(const char *fmt, ...);
void debug_vprint(const char *fmt, va_list args);
void debug_print(const char *fmt, ...);

#endif
