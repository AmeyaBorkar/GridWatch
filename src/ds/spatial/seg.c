/* seg.c — segment tree for range sum. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>

/* SEGMENT TREE — IMPLICIT ARRAY LAYOUT: a complete binary tree stored in an
 * array of size ~4n. Node index 1 is the root (1-indexed), so children of node v
 * are 2v and 2v+1. Each internal node stores the sum of its segment, supporting
 * range-sum queries and point updates in O(log n). Used for rolling 60-second
 * incident counts so the TUI can show live activity histograms cheaply. */
typedef struct sp_seg {
    size_t n;
    long long* tree;
} sp_seg_t;

/* BUILD: allocate the implicit-tree array. Size 4n is the safe upper bound
 * because the tree may be padded to the next power of two. Initialised to 0. */
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

/* POINT UPDATE: recurse down to the leaf for index `idx`, write the new value,
 * and recompute parent sums on the way back up. O(log n). */
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

/* RANGE-SUM QUERY: classic 3-case recursion — fully outside (return 0), fully
 * inside (return cached node sum), or partial overlap (recurse on both halves).
 * Visits O(log n) nodes. */
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
