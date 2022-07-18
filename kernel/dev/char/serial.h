#ifndef _DEV__CHAR__SERIAL_H
#define _DEV__CHAR__SERIAL_H

void serial_init(void);
void serial_out(char ch);
void serial_outstr(const char *str);

#endif
