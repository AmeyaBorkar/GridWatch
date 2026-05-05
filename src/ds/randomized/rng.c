/* rng.c — shared splitmix64 PRNG for the randomized module. */
#include <stdint.h>
#include <time.h>
#include "dispatch/randomized.h"

/* The below block uses SPLITMIX64 PRNG STATE — a single 64-bit word that
 * generates the entire random stream feeding skip lists, treaps, and any
 * other randomized DS. Sharing one stream means a single seed deterministically
 * reproduces every coin flip the simulation will make. */
static uint64_t g_state = 0;
static int      g_seeded = 0;

/* The below block uses the SPLITMIX64 STEP FUNCTION: advances the state by a
 * fixed odd 64-bit increment (the golden-ratio constant) and finalizes with
 * two xorshift-multiply rounds for good avalanche. Cheap, deterministic, and
 * good enough quality for randomized data structures. */
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

/* The below block exposes DETERMINISTIC SEEDING. Fixing the seed (e.g. from
 * a CLI flag or saved scenario) makes every randomized DS produce the exact
 * same shape across runs — essential for reproducible replays of incidents
 * and for debugging timing-sensitive bugs in the dispatcher. */
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

/* The below block lets each randomized container hold its OWN PRNG state and
 * advance it independently. Per-instance state means a single skip list or
 * treap can be deterministically replayed even if other DSes are also active. */
/* Per-instance helper: advance a user-owned state. */
uint64_t rnd__mix_u64(uint64_t* state);
uint64_t rnd__mix_u64(uint64_t* state) {
    return splitmix64_next(state);
}

/* The below block uses the GLOBAL STREAM to spawn fresh per-instance seeds.
 * Drawing seeds from one well-mixed source guarantees instance streams are
 * decorrelated yet still fully determined by the original master seed. */
/* Produce a fresh seed for per-instance RNG from the global stream. */
uint64_t rnd__fresh_state(void);
uint64_t rnd__fresh_state(void) {
    ensure_seeded();
    uint64_t v = splitmix64_next(&g_state);
    if (v == 0) v = 0x123456789ABCDEF0ULL;
    return v;
}
