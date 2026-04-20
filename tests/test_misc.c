/* test_misc.c — tests for DSU, persistent list, and bit vector. */
#include "dispatch/misc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void test_dsu(void) {
    const size_t N = 100;
    misc_dsu_t* d = misc_dsu_create(N);
    assert(d);
    assert(misc_dsu_count(d) == N);
    for (size_t i = 0; i < N; i++) {
        assert(misc_dsu_find(d, i) == i);
        assert(misc_dsu_size(d, i) == 1);
    }

    srand(42);
    size_t prev_count = N;
    size_t merges = 0;
    while (misc_dsu_count(d) > 1) {
        size_t a = (size_t)rand() % N;
        size_t b = (size_t)rand() % N;
        size_t before = misc_dsu_count(d);
        int m = misc_dsu_union(d, a, b);
        size_t after = misc_dsu_count(d);
        if (m) {
            assert(after == before - 1);
            merges++;
        } else {
            assert(after == before);
            assert(misc_dsu_find(d, a) == misc_dsu_find(d, b));
        }
        assert(after <= prev_count);
        prev_count = after;
    }
    assert(misc_dsu_count(d) == 1);
    /* Any root must have size N. */
    size_t root = misc_dsu_find(d, 0);
    assert(misc_dsu_size(d, root) == N);
    for (size_t i = 0; i < N; i++) {
        assert(misc_dsu_find(d, i) == root);
    }
    assert(merges == N - 1);
    misc_dsu_destroy(d);
    printf("DSU: OK (%zu merges)\n", merges);
}

static void test_plist(void) {
    misc_plist_t* p = misc_plist_create();
    assert(p);

    /* Build branch A: push 0..99 */
    misc_pnode_t* a = NULL;
    for (size_t i = 0; i < 100; i++) {
        a = misc_plist_push(p, a, (ds_val_t)(uintptr_t)(i + 1));
        assert(a);
        assert(misc_plist_length(a) == i + 1);
    }
    misc_pnode_t* snapshot_a = a;
    assert(misc_plist_length(snapshot_a) == 100);

    /* Fork branch B from a (after 50 elements) */
    misc_pnode_t* a50 = a;
    for (size_t i = 0; i < 50; i++) a50 = misc_plist_tail(a50);
    assert(a50);
    assert(misc_plist_length(a50) == 50);

    misc_pnode_t* b = a50;
    for (size_t i = 0; i < 100; i++) {
        b = misc_plist_push(p, b, (ds_val_t)(uintptr_t)(1000 + i));
        assert(misc_plist_length(b) == 50 + i + 1);
    }

    /* Old version snapshot_a unchanged */
    assert(misc_plist_length(snapshot_a) == 100);
    misc_pnode_t* cur = snapshot_a;
    for (size_t i = 0; i < 100; i++) {
        ds_val_t v;
        assert(misc_plist_head(cur, &v) == DS_OK);
        /* head-pushed last, so newest (99+1=100) is at front */
        size_t expected = 100 - i;
        assert((uintptr_t)v == expected);
        cur = misc_plist_tail(cur);
    }
    assert(cur == NULL);

    /* Branch b head is 1099 */
    ds_val_t hv;
    assert(misc_plist_head(b, &hv) == DS_OK);
    assert((uintptr_t)hv == 1099);
    assert(misc_plist_length(b) == 150);

    misc_plist_destroy(p);
    printf("Plist: OK\n");
}

static size_t naive_rank1(const int* bits, size_t n, size_t idx) {
    if (idx > n) idx = n;
    size_t r = 0;
    for (size_t i = 0; i < idx; i++) r += (size_t)(bits[i] != 0);
    return r;
}

static size_t naive_select1(const int* bits, size_t n, size_t k) {
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) {
        if (bits[i]) {
            if (cnt == k) return i;
            cnt++;
        }
    }
    return (size_t)-1;
}

static void test_bv(void) {
    const size_t N = 1024;
    misc_bv_t* b = misc_bv_create(N);
    assert(b);

    int* ref = (int*)calloc(N, sizeof(int));
    assert(ref);

    srand(123);
    size_t target = 300;
    size_t placed = 0;
    while (placed < target) {
        size_t idx = (size_t)rand() % N;
        if (!ref[idx]) {
            ref[idx] = 1;
            misc_bv_set(b, idx, 1);
            placed++;
        }
    }
    /* verify get */
    for (size_t i = 0; i < N; i++) {
        assert(misc_bv_get(b, i) == ref[i]);
    }

    misc_bv_build(b);

    /* rank1 for all idx in [0, N] */
    for (size_t i = 0; i <= N; i++) {
        size_t got = misc_bv_rank1(b, i);
        size_t ex  = naive_rank1(ref, N, i);
        if (got != ex) {
            fprintf(stderr, "rank1(%zu) got %zu expected %zu\n", i, got, ex);
            assert(0);
        }
    }

    /* select1 for all k in [0, target) */
    for (size_t k = 0; k < target; k++) {
        size_t got = misc_bv_select1(b, k);
        size_t ex  = naive_select1(ref, N, k);
        if (got != ex) {
            fprintf(stderr, "select1(%zu) got %zu expected %zu\n", k, got, ex);
            assert(0);
        }
    }
    /* out of range */
    assert(misc_bv_select1(b, target) == (size_t)-1);

    size_t fp = misc_bv_size_bytes(b);
    assert(fp > 0);

    free(ref);
    misc_bv_destroy(b);
    printf("BV: OK (footprint=%zu bytes for %zu bits)\n", fp, N);
}

int main(void) {
    test_dsu();
    test_plist();
    test_bv();
    printf("All misc tests passed.\n");
    return 0;
}
