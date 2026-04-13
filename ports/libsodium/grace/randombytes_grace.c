#include "randombytes.h"

/* Provided by libgrace. This is a placeholder seed source, not CSPRNG-grade. */
uint64_t time_ms(void);

static uint64_t rng_state;
static uint64_t rng_counter;
static int rng_seeded;

static uint64_t
splitmix64_next(uint64_t *state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void
randombytes_grace_stir(void)
{
    uint64_t seed = time_ms();

    seed ^= rng_counter++ * 0x9e3779b97f4a7c15ULL;

    if (!rng_seeded) {
        rng_state = seed ^ 0x6a09e667f3bcc909ULL;
        rng_seeded = 1;
    } else {
        rng_state ^= seed;
    }
}

static uint32_t
randombytes_grace_random(void)
{
    if (!rng_seeded) {
        randombytes_grace_stir();
    }
    return (uint32_t)(splitmix64_next(&rng_state) >> 32);
}

static void
randombytes_grace_buf(void *buf, const size_t size)
{
    unsigned char *p = (unsigned char *) buf;
    size_t remaining = size;

    if (!rng_seeded) {
        randombytes_grace_stir();
    }

    while (remaining > 0U) {
        uint64_t v = splitmix64_next(&rng_state);
        size_t chunk = remaining < sizeof v ? remaining : sizeof v;
        size_t i;

        for (i = 0U; i < chunk; i++) {
            p[i] = (unsigned char)(v & 0xffU);
            v >>= 8;
        }
        p += chunk;
        remaining -= chunk;
    }
}

static const char *
randombytes_grace_implementation_name(void)
{
    return "graceos-time";
}

struct randombytes_implementation randombytes_grace_implementation = {
    randombytes_grace_implementation_name,
    randombytes_grace_random,
    randombytes_grace_stir,
    NULL,
    randombytes_grace_buf,
    NULL
};

/* Compatibility symbols for libsodium headers. */
struct randombytes_implementation randombytes_sysrandom_implementation = {
    randombytes_grace_implementation_name,
    randombytes_grace_random,
    randombytes_grace_stir,
    NULL,
    randombytes_grace_buf,
    NULL
};

struct randombytes_implementation randombytes_internal_implementation = {
    randombytes_grace_implementation_name,
    randombytes_grace_random,
    randombytes_grace_stir,
    NULL,
    randombytes_grace_buf,
    NULL
};
