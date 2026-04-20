#include "dispatch/heaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

static double frand(void) { return (double)rand() / (double)RAND_MAX; }

static int test_fib(void) {
    heap_fib_t* h = heap_fib_create();
    if (!h) return 0;
    const int N = 1000;
    for (int i = 0; i < N; i++) {
        if (!heap_fib_push(h, frand() * 1e6, NULL)) return 0;
    }
    if (heap_fib_size(h) != (size_t)N) return 0;
    double prev = -INFINITY;
    for (int i = 0; i < N; i++) {
        ds_entry_t e;
        if (heap_fib_pop_min(h, &e) != DS_OK) return 0;
        if (e.key < prev) return 0;
        prev = e.key;
    }
    if (heap_fib_size(h) != 0) return 0;
    heap_fib_destroy(h);
    return 1;
}

static int test_fib_decrease(void) {
    heap_fib_t* h = heap_fib_create();
    heap_fib_node_t* nodes[50];
    for (int i = 0; i < 50; i++) {
        nodes[i] = heap_fib_push(h, 100.0 + i, NULL);
    }
    if (heap_fib_decrease_key(h, nodes[49], -5.0) != DS_OK) return 0;
    ds_entry_t e;
    if (heap_fib_peek_min(h, &e) != DS_OK) return 0;
    if (e.key != -5.0) return 0;
    heap_fib_destroy(h);
    return 1;
}

#define TEST_STD(NAME, TYPE, CREATE, PUSH, POP, DESTROY)               \
    static int test_##NAME(void) {                                      \
        TYPE* h = CREATE();                                             \
        if (!h) return 0;                                               \
        double vals[100];                                               \
        for (int i = 0; i < 100; i++) {                                 \
            vals[i] = frand() * 1000.0;                                 \
            if (PUSH(h, vals[i], NULL) != DS_OK) return 0;              \
        }                                                               \
        double prev = -INFINITY;                                        \
        for (int i = 0; i < 100; i++) {                                 \
            ds_entry_t e;                                               \
            if (POP(h, &e) != DS_OK) return 0;                          \
            if (e.key < prev) return 0;                                 \
            prev = e.key;                                               \
        }                                                               \
        DESTROY(h);                                                     \
        return 1;                                                       \
    }

TEST_STD(binom,   heap_binom_t,   heap_binom_create,   heap_binom_push,   heap_binom_pop_min,   heap_binom_destroy)
TEST_STD(leftist, heap_leftist_t, heap_leftist_create, heap_leftist_push, heap_leftist_pop_min, heap_leftist_destroy)
TEST_STD(skew,    heap_skew_t,    heap_skew_create,    heap_skew_push,    heap_skew_pop_min,    heap_skew_destroy)
TEST_STD(pairing, heap_pairing_t, heap_pairing_create, heap_pairing_push, heap_pairing_pop_min, heap_pairing_destroy)

static int test_depq(void) {
    depq_t* d = depq_create();
    if (!d) return 0;
    const int N = 200;
    for (int i = 0; i < N; i++) {
        if (depq_push(d, frand() * 1000.0, NULL) != DS_OK) return 0;
    }
    if (depq_size(d) != (size_t)N) return 0;
    double lo = -INFINITY, hi = INFINITY;
    int take_min = 1;
    while (depq_size(d) > 0) {
        ds_entry_t e;
        if (take_min) {
            if (depq_pop_min(d, &e) != DS_OK) return 0;
            if (e.key < lo) return 0;
            if (e.key > hi) return 0;
            lo = e.key;
        } else {
            if (depq_pop_max(d, &e) != DS_OK) return 0;
            if (e.key > hi) return 0;
            if (e.key < lo) return 0;
            hi = e.key;
        }
        take_min = !take_min;
    }
    depq_destroy(d);
    return 1;
}

int main(void) {
    srand(42);
    int ok = 1;
    ok &= test_fib();            printf("fib:     %s\n", ok ? "ok" : "FAIL");
    ok &= test_fib_decrease();   printf("fib_dk:  %s\n", ok ? "ok" : "FAIL");
    ok &= test_binom();          printf("binom:   %s\n", ok ? "ok" : "FAIL");
    ok &= test_leftist();        printf("leftist: %s\n", ok ? "ok" : "FAIL");
    ok &= test_skew();           printf("skew:    %s\n", ok ? "ok" : "FAIL");
    ok &= test_pairing();        printf("pairing: %s\n", ok ? "ok" : "FAIL");
    ok &= test_depq();           printf("depq:    %s\n", ok ? "ok" : "FAIL");
    if (!ok) { printf("FAIL\n"); return 1; }
    printf("PASS: all heap tests passed\n");
    return 0;
}
