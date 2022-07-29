#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include <lib/alloc.h>
#include <lib/lock.h>
#include <lib/misc.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <lib/vector.h>
#include <time/time.h>
#include <dev/pit.h>

static volatile struct limine_boot_time_request boot_time_request = {
    .id = LIMINE_BOOT_TIME_REQUEST,
    .revision = 0
};

struct timespec time_monotonic = {0, 0};
struct timespec time_realtime = {0, 0};

static spinlock_t timers_lock = SPINLOCK_INIT;
static VECTOR_TYPE(struct timer *) armed_timers = VECTOR_INIT;

struct timer *timer_new(struct timespec when) {
    struct timer *timer = ALLOC(struct timer);
    if (timer == NULL) {
        return NULL;
    }

    timer->when = when;
    timer_arm(timer);
    return timer;
}

void timer_arm(struct timer *timer) {
    spinlock_acquire(&timers_lock);

    timer->index = armed_timers.length;
    timer->fired = false;

    VECTOR_PUSH_BACK(&armed_timers, timer);
    spinlock_release(&timers_lock);
}

void timer_disarm(struct timer *timer) {
    spinlock_acquire(&timers_lock);

    if (armed_timers.length == 0 || timer->index == -1 || (size_t)timer->index >= armed_timers.length) {
        goto cleanup;
    }

    VECTOR_ITEM(&armed_timers, timer->index) = VECTOR_ITEM(&armed_timers, armed_timers.length - 1);
    VECTOR_ITEM(&armed_timers, timer->index)->index = timer->index;
    VECTOR_REMOVE(&armed_timers, armed_timers.length - 1);

    timer->index = -1;

cleanup:
    spinlock_release(&timers_lock);
}

void time_init(void) {
    struct limine_boot_time_response *boot_time_resp = boot_time_request.response;
    ASSERT(boot_time_resp != NULL);

    time_monotonic.tv_sec = boot_time_resp->boot_time;
    time_realtime.tv_sec = boot_time_resp->boot_time;

    pit_init();
}

void timer_handler(void) {
    struct timespec interval = {
        .tv_sec = 0,
        .tv_nsec = 1000000000 / TIMER_FREQ
    };

    time_monotonic = timespec_add(time_monotonic, interval);
    time_realtime = timespec_add(time_realtime, interval);

    if (spinlock_test_and_acq(&timers_lock)) {
        for (size_t i = 0; i < armed_timers.length; i++) {
            struct timer *timer = VECTOR_ITEM(&armed_timers, i);
            if (timer->fired) {
                continue;
            }

            timer->when = timespec_sub(timer->when, interval);
            if (timer->when.tv_sec == 0 && timer->when.tv_nsec == 0) {
                event_trigger(&timer->event, false);
                timer->fired = true;
            }
        }

        spinlock_release(&timers_lock);
    }
}
