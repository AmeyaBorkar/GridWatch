/* test_randomized.c — smoke tests for skip list and treap. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "dispatch/randomized.h"

#define N 1000

/* Deterministic pseudo-random key stream for reproducibility. */
static uint64_t test_state = 0xC0FFEE123456789ULL;
static double next_key(void) {
    /* xorshift64* */
    uint64_t x = test_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    test_state = x;
    uint64_t v = x * 0x2545F4914F6CDD1DULL;
    return (double)(v % 1000000U) + ((double)((v >> 20) % 1000U)) / 1000.0;
}

static int test_skip(void) {
    rnd_skip_t* s = rnd_skip_create();
    if (!s) { fprintf(stderr, "skip_create failed\n"); return 1; }

    ds_key_t keys[N];
    for (size_t i = 0; i < N; ++i) {
        ds_key_t k = next_key();
        rnd_skip_insert(s, k, (ds_val_t)(uintptr_t)(i + 1));
        keys[i] = k;
    }
    size_t sz = rnd_skip_size(s);
    printf("skip size after inserts: %zu\n", sz);
    if (sz == 0 || sz > N) { fprintf(stderr, "bad skip size\n"); rnd_skip_destroy(s); return 1; }

    /* top(10) must be sorted ascending and not exceed size. */
    ds_entry_t top[10];
    size_t got = rnd_skip_top(s, 10, top);
    if (got != (sz < 10 ? sz : 10)) { fprintf(stderr, "top count wrong: %zu\n", got); rnd_skip_destroy(s); return 1; }
    for (size_t i = 1; i < got; ++i) {
        if (top[i-1].key > top[i].key) { fprintf(stderr, "top not sorted\n"); rnd_skip_destroy(s); return 1; }
    }
    printf("skip top(%zu) sorted OK\n", got);

    /* Get for a known-inserted key. */
    ds_val_t v = NULL;
    if (rnd_skip_get(s, keys[0], &v) != DS_OK) {
        fprintf(stderr, "skip_get failed for keys[0]\n"); rnd_skip_destroy(s); return 1;
    }

    /* Delete half of the unique keys currently in the structure. */
    size_t to_delete = sz / 2;
    size_t deleted = 0;
    for (size_t i = 0; i < N && deleted < to_delete; ++i) {
        if (rnd_skip_delete(s, keys[i]) == DS_OK) ++deleted;
    }
    size_t after = rnd_skip_size(s);
    printf("skip size after deleting %zu: %zu (expected %zu)\n", deleted, after, sz - deleted);
    if (after != sz - deleted) { fprintf(stderr, "size mismatch after delete\n"); rnd_skip_destroy(s); return 1; }

    rnd_skip_destroy(s);
    return 0;
}

static int test_treap(void) {
    rnd_treap_t* t = rnd_treap_create();
    if (!t) { fprintf(stderr, "treap_create failed\n"); return 1; }

    ds_key_t keys[N];
    for (size_t i = 0; i < N; ++i) {
        ds_key_t k = next_key();
        rnd_treap_insert(t, k, (ds_val_t)(uintptr_t)(i + 1));
        keys[i] = k;
    }
    size_t sz = rnd_treap_size(t);
    printf("treap size: %zu\n", sz);
    if (sz == 0 || sz > N) { fprintf(stderr, "bad treap size\n"); rnd_treap_destroy(t); return 1; }

    ds_val_t v = NULL;
    if (rnd_treap_get(t, keys[0], &v) != DS_OK) {
        fprintf(stderr, "treap_get failed\n"); rnd_treap_destroy(t); return 1;
    }
    if (rnd_treap_get(t, -99999.0, &v) != DS_ERR_NOT_FOUND) {
        fprintf(stderr, "treap_get should have missed\n"); rnd_treap_destroy(t); return 1;
    }

    size_t target = sz / 2;
    size_t deleted = 0;
    for (size_t i = 0; i < N && deleted < target; ++i) {
        if (rnd_treap_delete(t, keys[i]) == DS_OK) ++deleted;
    }
    size_t after = rnd_treap_size(t);
    printf("treap size after deleting %zu: %zu\n", deleted, after);
    if (after != sz - deleted) { fprintf(stderr, "treap size mismatch\n"); rnd_treap_destroy(t); return 1; }

    rnd_treap_destroy(t);
    return 0;
}

int main(void) {
    rnd_seed(42);
    if (test_skip())  { printf("SKIP FAIL\n");  return 1; }
    if (test_treap()) { printf("TREAP FAIL\n"); return 1; }
    printf("ALL PASS\n");
    return 0;
}
