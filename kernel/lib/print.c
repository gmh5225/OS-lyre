#include <stdbool.h>
#include <stdint.h>
#include <dev/char/serial.h>
#include <lib/lock.h>
#include <lib/print.h>

static const char *base_digits = "0123456789abcdef";

static inline void print_char(char *buffer, size_t size, size_t *offset, char ch) {
    if (*offset < size - 1) {
        buffer[(*offset)++] = ch;
    }
    buffer[*offset] = 0;
}

static inline void print_str(char *buffer, size_t size, size_t *offset, const char *str) {
    while (*offset < size - 1 && *str) {
        buffer[(*offset)++] = *str++;
    }
    buffer[*offset] = 0;
}

static inline void print_nstr(char *buffer, size_t size, size_t *offset, const char *str, size_t len) {
    while (*offset < size - 1 && len > 0) {
        buffer[(*offset)++] = *str++;
        len--;
    }
    buffer[*offset] = 0;
}

static void print_int(char *buffer, size_t size, size_t *offset, int64_t value) {
    if (!value) {
        print_char(buffer, size, offset, '0');
        return;
    }

    int i;
    char temp_buffer[21] = {0};
    bool sign = value < 0;

    if (sign) {
        value = -value;
    }

    for (i = 19; value; i--) {
        temp_buffer[i] = (value % 10) + '0';
        value = value / 10;
    }

    if (sign) {
        temp_buffer[i] = '-';
    } else {
        i++;
    }

    print_str(buffer, size, offset, temp_buffer + i);
}

static void print_uint(char *buffer, size_t size, size_t *offset, uint64_t value) {
    if (!value) {
        print_char(buffer, size, offset, '0');
        return;
    }

    int i;
    char temp_buffer[21] = {0};

    for (i = 19; value; i--) {
        temp_buffer[i] = (value % 10) + '0';
        value = value / 10;
    }

    i++;
    print_str(buffer, size, offset, temp_buffer + i);
}

static void print_hex(char *buffer, size_t size, size_t *offset, uint64_t value) {
    if (!value) {
        print_str(buffer, size, offset, "0x0");
        return;
    }

    int i;
    char temp_buffer[17] = {0};

    for (i = 15; value; i--) {
        temp_buffer[i] = base_digits[value % 16];
        value = value / 16;
    }

    i++;
    print_str(buffer, size, offset, "0x");
    print_str(buffer, size, offset, temp_buffer + i);
}

size_t vsnprint(char *buffer, size_t size, const char *fmt, va_list args) {
    size_t offset = 0;

    while (offset < size - 1) {
        while (*fmt && *fmt != '%') {
            print_char(buffer, size, &offset, *fmt++);
        }

        if (!*fmt++ || offset == size - 1) {
            goto out;
        }

        bool long_arg = false;

parse_flags:
        char ch = *fmt++;

        switch (ch) {
            case 'l':
                long_arg = true;
                goto parse_flags;
            case 's': {
                char *str = va_arg(args, char *);
                if (str) {
                    print_str(buffer, size, &offset, str);
                } else {
                    print_str(buffer, size, &offset, "(null)");
                }
                break;
            }
            case 'S': {
                char *str = va_arg(args, char *);
                size_t len = va_arg(args, size_t);
                if (str) {
                    print_nstr(buffer, size, &offset, str, len);
                } else {
                    print_str(buffer, size, &offset, "(null)");
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                print_char(buffer, size, &offset, c);
                break;
            }
            case 'i':
            case 'd': {
                int64_t value;
                if (long_arg) {
                    value = va_arg(args, int64_t);
                } else {
                    value = va_arg(args, int32_t);
                }
                print_int(buffer, size, &offset, value);
                break;
            }
            case 'u': {
                uint64_t value;
                if (long_arg) {
                    value = va_arg(args, uint64_t);
                } else {
                    value = va_arg(args, uint32_t);
                }
                print_uint(buffer, size, &offset, value);
                break;
            }
            case 'x': {
                uint64_t value;
                if (long_arg) {
                    value = va_arg(args, uint64_t);
                } else {
                    value = va_arg(args, uint32_t);
                }
                print_hex(buffer, size, &offset, value);
                break;
            }
            default:
                print_char(buffer, size, &offset, '?');
                break;
        }
    }

out:
    return offset;
}

size_t snprint(char *buffer, size_t size, const char *fmt, ...) {
    va_list args;
    size_t ret;

    va_start(args, fmt);
    ret = vsnprint(buffer, size, fmt, args);
    va_end(args);

    return ret;
}

void vprint(const char *fmt, va_list args) {
    static spinlock_t lock = SPINLOCK_INIT;

    spinlock_acquire(&lock);

    char buffer[1024];
    vsnprint(buffer, sizeof(buffer), fmt, args);
    serial_outstr(buffer);

    spinlock_release(&lock);
}

void print(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vprint(fmt, args);
    va_end(args);
}
