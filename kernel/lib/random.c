#include <stddef.h>
#include <stdint.h>
#include <lib/lock.h>
#include <lib/panic.h>
#include <lib/random.h>
#include <time/time.h>

// Default parameters as per https://en.wikipedia.org/wiki/Mersenne_Twister
// Implemenation inspired by the pseudo code found on the linked page.

#define MT_W 64
#define MT_N 312
#define MT_M 156
#define MT_R 31
#define MT_A 0xB5026F5AA96619E9
#define MT_U 29
#define MT_D 0x5555555555555555
#define MT_S 17
#define MT_B 0x71D67FFFEDA60000
#define MT_T 37
#define MT_C 0xFFF7EEE000000000
#define MT_L 43
#define MT_F 0X5851F42D4C957F2D

#define MT_LOWER_MASK ((uint64_t)(1 << MT_R) - 1)
#define MT_UPPER_MASK ~(MT_LOWER_MASK)

static struct smartlock lock = SMARTLOCK_INIT;
static uint64_t mt[MT_N];
static size_t index = MT_N;

static void twist(void) {
    for (int i = 0; i < MT_N - 1; i++) {
        uint64_t x = (mt[i] & MT_UPPER_MASK) + (mt[(i + 1) % MT_N] & MT_LOWER_MASK);
        uint64_t x_a = x >> 1;
        if ((x & 1) != 0) {
            x_a ^= MT_A;
        }

        mt[i] = mt[(i + MT_M) % MT_N] ^ x_a;
    }

    index = 0;
}

static uint64_t generate(void) {
    if (index >= MT_N) {
        twist();
    }

    uint64_t y = mt[index++];
    y ^= (y >> MT_U) & MT_D;
    y ^= (y >> MT_S) & MT_B;
    y ^= (y >> MT_T) & MT_C;
    y ^= (y >> MT_L);
    return y;
}

void random_init(void) {
    // XXX don't use time to seed the PRNG!!
    random_seed(time_realtime.tv_sec);
}

void random_seed(uint64_t seed) {
    smartlock_acquire(&lock);

    index = MT_N;
    mt[0] = seed;

    for (int i = 1; i < MT_N - 1; i++) {
        mt[i] = MT_F * (mt[i - 1] ^ (mt[i - 1] >> (MT_W - 2))) + i;
    }

    smartlock_release(&lock);
}

void random_fill(void *buf, size_t length) {
    smartlock_acquire(&lock);

    uint8_t *buf_u8 = buf;
    while (length >= 8) {
        uint64_t value = generate();
        buf_u8[0] = (uint8_t)(value & 0xFF);
        buf_u8[1] = (uint8_t)((value >> 8) & 0xFF);
        buf_u8[2] = (uint8_t)((value >> 16) & 0xFF);
        buf_u8[3] = (uint8_t)((value >> 24) & 0xFF);
        buf_u8[4] = (uint8_t)((value >> 32) & 0xFF);
        buf_u8[5] = (uint8_t)((value >> 40) & 0xFF);
        buf_u8[6] = (uint8_t)((value >> 48) & 0xFF);
        buf_u8[7] = (uint8_t)((value >> 56) & 0xFF);
        buf_u8 += 8;
        length -= 8;
    }

    if (length > 0) {
        uint64_t value = generate();
        for (size_t i = 0; i < length; i++) {
            buf_u8[i] = (uint8_t)(value & 0xFF);
            value >>= 8;
        }
    }

    smartlock_release(&lock);
}

uint64_t random_generate() {
    smartlock_acquire(&lock);
    uint64_t result = generate();
    smartlock_release(&lock);
    return result;
}