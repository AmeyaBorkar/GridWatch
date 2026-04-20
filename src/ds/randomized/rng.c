/* rng.c — shared splitmix64 PRNG for the randomized module. */
#include <stdint.h>
#include <time.h>
#include "dispatch/randomized.h"

static uint64_t g_state = 0;
static int      g_seeded = 0;

static uint64_t splitmix64_next(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void ensure_seeded(void) {
    if (!g_seeded) {
        g_state = (uint64_t)time(NULL) ^ 0xA5A5A5A5DEADBEEFULL;
        g_seeded = 1;
    }
}

void rnd_seed(uint64_t s) {
    g_state = s ? s : 0xDEADBEEFCAFEBABEULL;
    g_seeded = 1;
}

/* Internal API (not exposed in header). */
uint64_t rnd__next_u64(void);
uint64_t rnd__next_u64(void) {
    ensure_seeded();
    return splitmix64_next(&g_state);
}

/* Per-instance helper: advance a user-owned state. */
uint64_t rnd__mix_u64(uint64_t* state);
uint64_t rnd__mix_u64(uint64_t* state) {
    return splitmix64_next(state);
}

/* Produce a fresh seed for per-instance RNG from the global stream. */
uint64_t rnd__fresh_state(void);
uint64_t rnd__fresh_state(void) {
    ensure_seeded();
    uint64_t v = splitmix64_next(&g_state);
    if (v == 0) v = 0x123456789ABCDEF0ULL;
    return v;
}
