#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <limine.h>
#include <lib/libc.h>
#include <lib/resource.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <lib/lock.h>
#include <lib/event.h>
#include <mm/vmm.h>
#include <dev/char/console.h>
#include <dev/ps2.h>
#include <fs/devtmpfs.h>
#include <sys/cpu.h>
#include <sys/int_events.h>
#include <sched/sched.h>
#include <abi-bits/termios.h>
#include <abi-bits/poll.h>

static bool is_printable(uint8_t c) {
    return c >= 0x20 && c <= 0x7e;
}

static void limine_term_callback(struct limine_terminal *term, uint64_t t, uint64_t a, uint64_t b, uint64_t c);

static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0,
    .callback = limine_term_callback
};

struct console {
    struct resource;
    struct termios termios;
    bool decckm;
};

static spinlock_t read_lock;
static struct event console_event;
static struct console *console_res;

static const char convtab_capslock[] = {
    '\0', '\e', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', '\n', '\0', 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`', '\0', '\\', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', ',', '.', '/', '\0', '\0', '\0', ' '
};

static const char convtab_shift[] = {
    '\0', '\e', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', '\0', 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', '\0', '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', '\0', '\0', '\0', ' '
};

static const char convtab_shift_capslock[] = {
    '\0', '\e', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '{', '}', '\n', '\0', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ':', '"', '~', '\0', '|', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', '<', '>', '?', '\0', '\0', '\0', ' '
};

static const char convtab_nomod[] = {
    '\0', '\e', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', '\0', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', '\0', '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', '\0', '\0', '\0', ' '
};

static ssize_t tty_read(struct resource *_this, void *buf, off_t offset, size_t count) {
    (void)_this; (void)buf; (void)offset;

    return count;
}

static ssize_t tty_write(struct resource *_this, const void *buf, off_t offset, size_t count) {
    (void)offset;
    (void)_this;

    char *local = alloc(count);
    memcpy(local, buf, count);

    uint64_t cr3 = read_cr3();
    if (cr3 != (uint64_t)vmm_kernel_pagemap->top_level - VMM_HIGHER_HALF) {
        vmm_switch_to(vmm_kernel_pagemap);
    }

    terminal_request.response->write(terminal_request.response->terminals[0], local, count);

    if (cr3 != (uint64_t)vmm_kernel_pagemap->top_level - VMM_HIGHER_HALF) {
        write_cr3(cr3);
    }

    free(local);

    return count;
}

#define SCANCODE_MAX 0x57
#define SCANCODE_CTRL 0x1d
#define SCANCODE_CTRL_REL 0x9d
#define SCANCODE_SHIFT_RIGHT 0x36
#define SCANCODE_SHIFT_RIGHT_REL 0xb6
#define SCANCODE_SHIFT_LEFT 0x2a
#define SCANCODE_SHIFT_LEFT_REL 0xaa
#define SCANCODE_ALT_LEFT 0x38
#define SCANCODE_ALT_LEFT_REL 0xb8
#define SCANCODE_CAPSLOCK 0x3a
#define SCANCODE_NUMLOCK 0x45

#define KBD_BUFFER_SIZE 1024
#define KBD_BIGBUF_SIZE 4096

static char kbd_buffer[KBD_BUFFER_SIZE];
static size_t kbd_buffer_i = 0;
static char kbd_bigbuf[KBD_BIGBUF_SIZE];
static size_t kbd_bigbuf_i = 0;

static void add_to_buf_char(char c, bool echo) {
    if (c == '\n' && (console_res->termios.c_iflag & ICRNL) == 0) {
        c = '\r';
    }

    if (console_res->termios.c_lflag & ICANON) {
        switch (c) {
            case '\n': {
                if (kbd_buffer_i == KBD_BUFFER_SIZE) {
                    return;
                }
                kbd_buffer[kbd_buffer_i++] = c;
                if (echo && (console_res->termios.c_lflag & ECHO)) {
                    tty_write(NULL, "\n", 0, 1);
                }
                for (size_t i = 0; i < kbd_buffer_i; i++) {
                    if ((console_res->status & POLLIN) == 0) {
                        console_res->status |= POLLIN;
                        event_trigger(&console_res->event, false);
                    }
                    if (kbd_bigbuf_i == KBD_BIGBUF_SIZE) {
                        return;
                    }
                    kbd_bigbuf[kbd_bigbuf_i++] = kbd_buffer[i];
                }
                kbd_buffer_i = 0;
                return;
            }
            case '\b': {
                if (kbd_buffer_i == 0) {
                    return;
                }
                kbd_buffer_i--;
                size_t to_backspace;
                if (kbd_buffer[kbd_buffer_i] >= 0x01 && kbd_buffer[kbd_buffer_i] <= 0x1a) {
                    to_backspace = 2;
                } else {
                    to_backspace = 1;
                }
                kbd_buffer[kbd_buffer_i] = 0;
                if (echo && (console_res->termios.c_lflag & ECHO) != 0) {
                    for (size_t i = 0; i < to_backspace; i++) {
                        tty_write(NULL, "\b \b", 0, 3);
                    }
                }
                return;
            }
        }

        if (kbd_buffer_i == KBD_BUFFER_SIZE) {
            return;
        }
        kbd_buffer[kbd_buffer_i++] = c;
    } else {
        if ((console_res->status & POLLIN) == 0) {
            console_res->status |= POLLIN;
            event_trigger(&console_res->event, false);
        }
        if (kbd_bigbuf_i == KBD_BIGBUF_SIZE) {
            return;
        }
        kbd_bigbuf[kbd_bigbuf_i++] = c;
    }

    if (echo && (console_res->termios.c_lflag & ECHO) != 0) {
        if (is_printable(c)) {
            tty_write(NULL, &c, 0, 1);
        } else if (c >= 0x01 && c <= 0x1a) {
            char caret[2];
            caret[0] = '^';
            caret[1] = c + 0x40;
            tty_write(NULL, caret, 0, 2);
        }
    }
}

static void add_to_buf(char *ptr, size_t count, bool echo) {
    spinlock_acquire(&read_lock);


    for (size_t i = 0; i < count; i++) {
        char c = ptr[i];
        if ((console_res->termios.c_lflag & ISIG) != 0) {
            if (c == (char)console_res->termios.c_cc[VINTR]) {
                // sendsig(latest_thread, sigint);
            }
        }
        add_to_buf_char(c, echo);
    }

    event_trigger(&console_event, false);

    spinlock_release(&read_lock);
}

static noreturn void keyboard_handler(void) {
    bool extra_scancodes = false;
    bool ctrl_active = false;
    //bool numlock_active = false;
    //bool alt_active = false;
    bool shift_active = false;
    bool capslock_active = false;

    for (;;) {
        struct event *events[] = { &int_events[ps2_keyboard_vector] };
        event_await(events, 1, true);
        uint8_t input_byte = ps2_read();

        if (input_byte == 0xe0) {
            extra_scancodes = true;
            continue;
        }

        if (extra_scancodes == true) {
            extra_scancodes = false;

            switch (input_byte) {
                case SCANCODE_CTRL:
                    ctrl_active = true;
                    continue;
                case SCANCODE_CTRL_REL:
                    ctrl_active = false;
                    continue;
                case 0x1c:
                    add_to_buf("\n", 1, true);
                    continue;
                case 0x35:
                    add_to_buf("/", 1, true);
                    continue;
                case 0x48: // up arrow
                    if (console_res->decckm == false) {
                        add_to_buf("\e[A", 3, true);
                    } else {
                        add_to_buf("\eOA", 3, true);
                    }
                    continue;
                case 0x4b: // left arrow
                    if (console_res->decckm == false) {
                        add_to_buf("\e[D", 3, true);
                    } else {
                        add_to_buf("\eOD", 3, true);
                    }
                    continue;
                case 0x50: // down arrow
                    if (console_res->decckm == false) {
                        add_to_buf("\e[B", 3, true);
                    } else {
                        add_to_buf("\eOB", 3, true);
                    }
                    continue;
                case 0x4d: // right arrow
                    if (console_res->decckm == false) {
                        add_to_buf("\e[C", 3, true);
                    } else {
                        add_to_buf("\eOC", 3, true);
                    }
                    continue;
                case 0x47: // home
                    add_to_buf("\e[1~", 4, true);
                    continue;
                case 0x4f: // end
                    add_to_buf("\e[4~", 4, true);
                    continue;
                case 0x49: // pgup
                    add_to_buf("\e[5~", 4, true);
                    continue;
                case 0x51: // pgdown
                    add_to_buf("\e[6~", 4, true);
                    continue;
                case 0x53: // delete
                    add_to_buf("\e[3~", 4, true);
                    continue;
            }
        }

        switch (input_byte) {
            case SCANCODE_NUMLOCK:
                //numlock_active = true;
                continue;
            case SCANCODE_ALT_LEFT:
                //alt_active = true;
                continue;
            case SCANCODE_ALT_LEFT_REL:
                //alt_active = false;
                continue;
            case SCANCODE_SHIFT_LEFT:
            case SCANCODE_SHIFT_RIGHT:
                shift_active = true;
                continue;
            case SCANCODE_SHIFT_LEFT_REL:
            case SCANCODE_SHIFT_RIGHT_REL:
                shift_active = false;
                continue;
            case SCANCODE_CTRL:
                ctrl_active = true;
                continue;
            case SCANCODE_CTRL_REL:
                ctrl_active = false;
                continue;
            case SCANCODE_CAPSLOCK:
                capslock_active = !capslock_active;
                continue;
        }

        char c = 0;

        if (input_byte < SCANCODE_MAX) {
            if (capslock_active == false && shift_active == false) {
                c = convtab_nomod[input_byte];
            }
            if (capslock_active == false && shift_active == true) {
                c = convtab_shift[input_byte];
            }
            if (capslock_active == true && shift_active == false) {
                c = convtab_capslock[input_byte];
            }
            if (capslock_active == true && shift_active == true) {
                c = convtab_shift_capslock[input_byte];
            }
        } else {
            continue;
        }

        if (ctrl_active) {
            c = toupper(c) - 0x40;
        }

        add_to_buf(&c, 1, true);
    }
}

static void dec_private(uint64_t esc_val_count, uint32_t *esc_values, uint64_t final) {
    (void)esc_val_count;

    print("dec private: ? %lu %c\n", esc_values[0], final);
    switch (esc_values[0]) {
        case 1:
            switch (final) {
                case 'h':
                    console_res->decckm = true;
                    break;
                case 'l':
                    console_res->decckm = false;
                    break;
            }
    }
}

static void limine_term_callback(struct limine_terminal *term, uint64_t t, uint64_t a, uint64_t b, uint64_t c) {
    (void)term;

    print("Limine terminal callback called\n");

    switch (t) {
        case 10:
            dec_private(a, (void *)b, c);
    }
}

void console_init(void) {
    struct limine_terminal_response *terminal_resp = terminal_request.response;

    if (terminal_resp == NULL || terminal_resp->terminal_count < 1) {
        // TODO: Should not be a hard requirement..?
        panic(NULL, true, "Limine terminal is not available");
    }

    console_res = resource_create(sizeof(struct console));

    console_res->stat.st_size = 0;
    console_res->stat.st_blocks = 0;
    console_res->stat.st_blksize = 512;
    console_res->stat.st_rdev = resource_create_dev_id();
    console_res->stat.st_mode = 0644 | S_IFCHR;

    // Termios initialisation
    console_res->termios.c_lflag = ISIG | ICANON | ECHO;
    console_res->termios.c_cc[VINTR] = 0x03;
    console_res->termios.ibaud = 38400;
    console_res->termios.obaud = 38400;

    console_res->status |= POLLOUT;

    console_res->read = tty_read;
    console_res->write = tty_write;

    devtmpfs_add_device((struct resource *)console_res, "console");

    sched_new_kernel_thread(keyboard_handler, NULL, true);
}
