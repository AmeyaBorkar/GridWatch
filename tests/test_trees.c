/* test_trees.c — sanity test for trees module. */
#include "dispatch/trees.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 1000

typedef struct {
    ds_key_t* buf;
    size_t    n;
    size_t    cap;
    int       ok;
    ds_key_t  prev;
    int       has_prev;
} collect_t;

static void collect_visit(ds_key_t k, ds_val_t v, void* user) {
    (void)v;
    collect_t* c = (collect_t*)user;
    if (c->n >= c->cap) { c->ok = 0; return; }
    if (c->has_prev && k <= c->prev) c->ok = 0;
    c->prev = k;
    c->has_prev = 1;
    c->buf[c->n++] = k;
}

static int cmp_d(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static void gen_distinct_keys(double* keys, size_t n) {
    /* generate n distinct doubles */
    for (size_t i = 0; i < n; i++) keys[i] = (double)i + 0.5;
    /* Fisher-Yates shuffle */
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        double t = keys[i]; keys[i] = keys[j]; keys[j] = t;
    }
}

static int test_avl(const double* keys) {
    tree_avl_t* t = tree_avl_create();
    assert(t);
    for (size_t i = 0; i < N; i++) {
        assert(tree_avl_insert(t, keys[i], NULL) == DS_OK);
    }
    assert(tree_avl_size(t) == N);

    double buf[N];
    collect_t c = { buf, 0, N, 1, 0, 0 };
    tree_avl_inorder(t, collect_visit, &c);
    tree_avl_destroy(t);
    return c.ok && c.n == N;
}

static int test_rb(const double* keys) {
    tree_rb_t* t = tree_rb_create();
    assert(t);
    for (size_t i = 0; i < N; i++) {
        assert(tree_rb_insert(t, keys[i], NULL) == DS_OK);
    }
    assert(tree_rb_size(t) == N);

    double buf[N];
    collect_t c = { buf, 0, N, 1, 0, 0 };
    tree_rb_inorder(t, collect_visit, &c);
    tree_rb_destroy(t);
    return c.ok && c.n == N;
}

static int test_splay(const double* keys) {
    tree_splay_t* t = tree_splay_create();
    assert(t);
    for (size_t i = 0; i < N; i++) {
        assert(tree_splay_insert(t, keys[i], NULL) == DS_OK);
    }
    assert(tree_splay_size(t) == N);

    double buf[N];
    collect_t c = { buf, 0, N, 1, 0, 0 };
    tree_splay_inorder(t, collect_visit, &c);
    tree_splay_destroy(t);
    return c.ok && c.n == N;
}

static int test_bplus(const double* keys) {
    tree_bplus_t* t = tree_bplus_create(8);
    assert(t);
    for (size_t i = 0; i < N; i++) {
        assert(tree_bplus_insert(t, keys[i], NULL) == DS_OK);
    }
    assert(tree_bplus_size(t) == N);

    double buf[N];
    collect_t c = { buf, 0, N, 1, 0, 0 };
    tree_bplus_inorder(t, collect_visit, &c);
    tree_bplus_destroy(t);
    return c.ok && c.n == N;
}

static int test_threaded(const double* keys) {
    tree_threaded_t* t = tree_threaded_create();
    assert(t);
    for (size_t i = 0; i < N; i++) {
        assert(tree_threaded_insert(t, keys[i], NULL) == DS_OK);
    }
    assert(tree_threaded_size(t) == N);

    double buf[N];
    collect_t c = { buf, 0, N, 1, 0, 0 };
    tree_threaded_inorder(t, collect_visit, &c);
    tree_threaded_destroy(t);
    return c.ok && c.n == N;
}

static int test_huffman(void) {
    /* ~500 bytes with skewed distribution so compression helps */
    size_t n = 500;
    uint8_t* data = (uint8_t*)malloc(n);
    for (size_t i = 0; i < n; i++) {
        int r = rand() % 100;
        if (r < 60) data[i] = 'a';
        else if (r < 80) data[i] = 'b';
        else if (r < 90) data[i] = 'c';
        else if (r < 95) data[i] = 'd';
        else data[i] = (uint8_t)('e' + rand() % 10);
    }

    huffman_t* h = huffman_build(data, n);
    assert(h);

    size_t cap = n * 2 + 16;
    uint8_t* enc = (uint8_t*)malloc(cap);
    size_t bits = huffman_encode(h, data, n, enc, cap);
    assert(bits > 0);

    uint8_t* dec = (uint8_t*)malloc(n + 16);
    size_t dn = huffman_decode(h, enc, bits, dec, n + 16);
    int ok = (dn == n) && (memcmp(dec, data, n) == 0);

    double ratio = huffman_ratio(h, data, n);
    printf("  huffman: %zu bytes -> %zu bits (ratio %.3f)\n", n, bits, ratio);

    free(dec);
    free(enc);
    huffman_destroy(h);
    free(data);
    return ok;
}

int main(void) {
    srand(42);
    double* keys = (double*)malloc(sizeof(double) * N);
    gen_distinct_keys(keys, N);

    /* expected sorted sequence */
    double sorted[N];
    memcpy(sorted, keys, sizeof(double) * N);
    qsort(sorted, N, sizeof(double), cmp_d);
    (void)sorted; /* just validate monotonicity in visitors */

    int ok = 1;
    ok &= test_avl(keys);         printf("AVL:       %s\n", ok ? "ok" : "FAIL");
    ok &= test_rb(keys);          printf("RB:        %s\n", ok ? "ok" : "FAIL");
    ok &= test_splay(keys);       printf("Splay:     %s\n", ok ? "ok" : "FAIL");
    ok &= test_bplus(keys);       printf("Bplus:     %s\n", ok ? "ok" : "FAIL");
    ok &= test_threaded(keys);    printf("Threaded:  %s\n", ok ? "ok" : "FAIL");
    ok &= test_huffman();         printf("Huffman:   %s\n", ok ? "ok" : "FAIL");

    free(keys);
    if (ok) { printf("PASS\n"); return 0; }
    printf("FAIL\n"); return 1;
}
