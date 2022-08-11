#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <lyre/memstat.h>
#include <lyre/syscall.h>

extern char *__progname;

#define SIZEOF_ARRAY(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

static const char *lfetch_logo[] = {
    "    ___",
    "   /\\__\\",
    "  /:/  /",
    " /:/__/",
    " \\:\\  \\",
    "  \\:\\__\\",
    "   \\/__/",
    "",
};

static char fmt_buffer[256] = {0};

static void print_line(int i, const char *name, const char *fmt, ...) {
    printf("\x1b[36;1m%-12s", lfetch_logo[i]);

    if (name != NULL) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(fmt_buffer, sizeof(fmt_buffer) - 1, fmt, args);
        va_end(args);

        printf("%s\x1b[0m: %s\n", name, fmt_buffer);
    } else {
        printf("\x1b[0m\n");
    }
}

static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    asm volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

#define KIB (1024)
#define MIB (1024 * KIB)
#define GIB (1024 * MIB)

static void convert_to_units(char buffer[4], size_t amount, size_t *total, size_t *fraction) {
    if (amount >= GIB) {
        strcpy(buffer, "GiB");
        *total = amount / GIB;
        *fraction = (amount % GIB) / MIB;
    } else if (amount >= MIB) {
        strcpy(buffer, "MiB");
        *total = amount / MIB;
        *fraction = (amount % MIB) / KIB;
    } else if (amount >= KIB) {
        strcpy(buffer, "KiB");
        *total = amount / KIB;
        *fraction = amount % KIB;
    } else {
        strcpy(buffer, "B");
        *total = amount;
        *fraction = 0;
    }

    *fraction /= 100;
}

int main(void) {
    struct utsname utsname;
    if (uname(&utsname) != 0) {
        fprintf(stderr, "%s: failed to get system name: %s\n", __progname, strerror(errno));
        exit(1);
    }

    char cpu_brand_string[49] = {0};
    cpuid(0x80000002, 0,
        (uint32_t*)cpu_brand_string + 0,
        (uint32_t*)cpu_brand_string + 1,
        (uint32_t*)cpu_brand_string + 2,
        (uint32_t*)cpu_brand_string + 3);
    cpuid(0x80000003, 0,
        (uint32_t*)cpu_brand_string + 4,
        (uint32_t*)cpu_brand_string + 5,
        (uint32_t*)cpu_brand_string + 6,
        (uint32_t*)cpu_brand_string + 7);
    cpuid(0x80000004, 0,
        (uint32_t*)cpu_brand_string + 8,
        (uint32_t*)cpu_brand_string + 9,
        (uint32_t*)cpu_brand_string + 10,
        (uint32_t*)cpu_brand_string + 11);

    struct lyre_kmemstat memstat;
    struct __syscall_ret ret = __syscall(SYS_getmemstat, &memstat);
    if ((int)ret.ret == -1) {
        fprintf(stderr, "%s: failed to get memory statistics: %s\n", __progname, strerror(ret.errno));
        exit(1);
    }

    struct timespec uptime = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &uptime) != 0) {
        fprintf(stderr, "%s: failed to get system uptime: %s\n", __progname, strerror(errno));
        exit(1);
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(lfetch_logo); i++) {
        switch (i) {
            case 1:
                print_line(i, "OS", "%s %s", utsname.sysname, utsname.machine);
                break;
            case 2:
                print_line(i, "Kernel", "%s", utsname.release);
                break;
            case 3:
                print_line(i, "CPU", "%s", cpu_brand_string);
                break;
            case 4: {
                char used_unit[4] = {0};
                char total_unit[4] = {0};

                size_t used_total, used_fraction;
                size_t total_total, total_fraction;

                convert_to_units(used_unit, memstat.n_phys_used, &used_total, &used_fraction);
                convert_to_units(total_unit, memstat.n_phys_total, &total_total, &total_fraction);

                char used_display[32] = {0};
                if (used_fraction > 0) {
                    sprintf(used_display, "%lu.%lu%s", used_total, used_fraction, used_unit);
                } else {
                    sprintf(used_display, "%lu%s", used_total, used_unit);
                }

                char total_display[32] = {0};
                if (total_fraction > 0) {
                    sprintf(total_display, "%lu.%lu%s", total_total, total_fraction, total_unit);
                } else {
                    sprintf(total_display, "%lu%s", total_total, total_unit);
                }

                print_line(i, "Memory", "%s/%s", used_display, total_display);
            } break;
            case 5: {
                char days_display[24] = {0}, hrs_display[24] = {0},
                     mins_display[24] = {0}, secs_display[24] = {0};

                time_t days = uptime.tv_sec / (3600 * 24);
                if (days > 0) {
                    sprintf(days_display, "%lu day%s, ", days, days == 1 ? "" : "s");
                }

                time_t hrs = (uptime.tv_sec % (3600 * 24)) / 3600;
                if (hrs > 0) {
                    sprintf(hrs_display, "%lu hr%s, ", hrs, hrs == 1 ? "" : "s");
                }

                time_t mins = (uptime.tv_sec % 3600) / 60;
                if (mins > 0) {
                    sprintf(mins_display, "%lu min%s, ", mins, mins == 1 ? "" : "s");
                }

                time_t secs = uptime.tv_sec % 60;
                sprintf(secs_display, "%lu sec%s", secs, secs == 1 ? "" : "s");
                print_line(i, "Uptime", "%s%s%s%s", days_display, hrs_display, mins_display, secs_display);
            } break;
            default:
                print_line(i, NULL, NULL);
                break;
        }
    }
}
