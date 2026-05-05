/* Skew heap: self-adjusting meldable heap; merge unconditionally swaps children. */
#include "dispatch/heaps.h"
#include <stdlib.h>

/* SKEW HEAP NODE: like a leftist node but with NO s-value bookkeeping —
 * the structural balance is enforced statistically by always swapping children
 * during meld. Cheaper per-op constants than leftist. */
typedef struct snode {
    ds_key_t key;
    ds_val_t val;
    struct snode *left, *right;
} snode;

struct heap_skew {
    snode* root;
    size_t n;
};

heap_skew_t* heap_skew_create(void) {
    return (heap_skew_t*)calloc(1, sizeof(heap_skew_t));
}

static void snode_free(snode* x) {
    if (!x) return;
    snode_free(x->left);
    snode_free(x->right);
    free(x);
}

void heap_skew_destroy(heap_skew_t* h) {
    if (!h) return;
    snode_free(h->root);
    free(h);
}

size_t heap_skew_size(const heap_skew_t* h) { return h ? h->n : 0; }

/* MELD: like leftist meld but UNCONDITIONALLY swaps children at every step.
 * No s-values, no checks — the swap-on-every-merge rule guarantees amortised
 * O(log n) per op (proved via potential function). The classic self-adjusting DS. */
static snode* smerge(snode* a, snode* b) {
    if (!a) return b;
    if (!b) return a;
    if (b->key < a->key) { snode* t = a; a = b; b = t; }
    snode* tmp = a->left;
    a->left = smerge(a->right, b);
    a->right = tmp;
    return a;
}

/* INSERT = MELD with a single-node tree (like leftist). */
ds_status_t heap_skew_push(heap_skew_t* h, ds_key_t key, ds_val_t val) {
    if (!h) return DS_ERR_INVALID;
    snode* n = (snode*)calloc(1, sizeof(*n));
    if (!n) return DS_ERR_NOMEM;
    n->key = key;
    n->val = val;
    h->root = smerge(h->root, n);
    h->n++;
    return DS_OK;
}

ds_status_t heap_skew_peek_min(const heap_skew_t* h, ds_entry_t* out) {
    if (!h || !out) return DS_ERR_INVALID;
    if (!h->root) return DS_ERR_EMPTY;
    out->key = h->root->key;
    out->val = h->root->val;
    return DS_OK;
}

/* POP-MIN: drop the root and meld its two subtrees. Same shape as leftist. */
ds_status_t heap_skew_pop_min(heap_skew_t* h, ds_entry_t* out) {
    if (!h) return DS_ERR_INVALID;
    if (!h->root) return DS_ERR_EMPTY;
    snode* r = h->root;
    if (out) { out->key = r->key; out->val = r->val; }
    h->root = smerge(r->left, r->right);
    free(r);
    h->n--;
    return DS_OK;
}

ds_status_t heap_skew_merge(heap_skew_t* dst, heap_skew_t* src) {
    if (!dst || !src) return DS_ERR_INVALID;
    dst->root = smerge(dst->root, src->root);
    dst->n += src->n;
    src->root = NULL;
    src->n = 0;
    return DS_OK;
}
