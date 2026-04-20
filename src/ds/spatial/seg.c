/* seg.c — segment tree for range sum. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>

typedef struct sp_seg {
    size_t n;
    long long* tree;
} sp_seg_t;

sp_seg_t* sp_seg_create(size_t n) {
    sp_seg_t* s = (sp_seg_t*)calloc(1, sizeof(sp_seg_t));
    if (!s) return NULL;
    s->n = n;
    if (n == 0) return s;
    s->tree = (long long*)calloc(4 * n, sizeof(long long));
    if (!s->tree) { free(s); return NULL; }
    return s;
}

void sp_seg_destroy(sp_seg_t* s) {
    if (!s) return;
    free(s->tree);
    free(s);
}

static void seg_update_rec(long long* tree, size_t node, size_t l, size_t r,
                            size_t idx, long long val) {
    if (l == r) {
        tree[node] = val;
        return;
    }
    size_t mid = (l + r) / 2;
    if (idx <= mid) seg_update_rec(tree, 2*node, l, mid, idx, val);
    else seg_update_rec(tree, 2*node+1, mid+1, r, idx, val);
    tree[node] = tree[2*node] + tree[2*node+1];
}

void sp_seg_update(sp_seg_t* s, size_t idx, long long value) {
    if (!s || s->n == 0 || idx >= s->n) return;
    seg_update_rec(s->tree, 1, 0, s->n - 1, idx, value);
}

static long long seg_query_rec(const long long* tree, size_t node, size_t l, size_t r,
                                size_t ql, size_t qr) {
    if (qr < l || r < ql) return 0;
    if (ql <= l && r <= qr) return tree[node];
    size_t mid = (l + r) / 2;
    return seg_query_rec(tree, 2*node, l, mid, ql, qr) +
           seg_query_rec(tree, 2*node+1, mid+1, r, ql, qr);
}

long long sp_seg_query(const sp_seg_t* s, size_t lo, size_t hi) {
    if (!s || s->n == 0) return 0;
    if (lo >= s->n) return 0;
    if (hi >= s->n) hi = s->n - 1;
    if (lo > hi) return 0;
    return seg_query_rec(s->tree, 1, 0, s->n - 1, lo, hi);
}
