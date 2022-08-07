#ifndef _DEV__CHAR__CONSOLE_H
#define _DEV__CHAR__CONSOLE_H

#include <stddef.h>

void console_init(void);
void console_write(const char *buf, size_t length);

#endif
