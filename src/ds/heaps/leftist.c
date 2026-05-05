/* Leftist tree: heap-ordered binary tree with s-value (null-path length) invariant. */
#include "dispatch/heaps.h"
#include <stdlib.h>

/* LEFTIST NODE: heap-ordered binary tree node. Field 's' is the s-value
 * (null-path length) — distance to the nearest null descendant on the right
 * spine. The leftist invariant s(left) >= s(right) keeps the right spine short
 * (O(log n)), so meld can recurse down only the right spine. */
typedef struct lnode {
    ds_key_t key;
    ds_val_t val;
    int s;
    struct lnode *left, *right;
} lnode;

struct heap_leftist {
    lnode* root;
    size_t n;
};

heap_leftist_t* heap_leftist_create(void) {
    return (heap_leftist_t*)calloc(1, sizeof(heap_leftist_t));
}

static void lnode_free(lnode* x) {
    if (!x) return;
    lnode_free(x->left);
    lnode_free(x->right);
    free(x);
}

void heap_leftist_destroy(heap_leftist_t* h) {
    if (!h) return;
    lnode_free(h->root);
    free(h);
}

size_t heap_leftist_size(const heap_leftist_t* h) { return h ? h->n : 0; }

/* MELD: the heart of every leftist op. Recursively merges along the right spines,
 * then restores the leftist property by swapping children if s(left) < s(right),
 * and recomputes s. O(log n) because right spines are O(log n). */
static lnode* lmerge(lnode* a, lnode* b) {
    if (!a) return b;
    if (!b) return a;
    if (b->key < a->key) { lnode* t = a; a = b; b = t; }
    a->right = lmerge(a->right, b);
    int ls = a->left  ? a->left->s  : 0;
    int rs = a->right ? a->right->s : 0;
    if (ls < rs) { lnode* t = a->left; a->left = a->right; a->right = t; }
    a->s = 1 + (a->right ? a->right->s : 0);
    return a;
}

/* INSERT = MELD with a single-node tree. Demonstrates how every operation on a
 * meldable heap reduces to meld. */
ds_status_t heap_leftist_push(heap_leftist_t* h, ds_key_t key, ds_val_t val) {
    if (!h) return DS_ERR_INVALID;
    lnode* n = (lnode*)calloc(1, sizeof(*n));
    if (!n) return DS_ERR_NOMEM;
    n->key = key;
    n->val = val;
    n->s = 1;
    h->root = lmerge(h->root, n);
    h->n++;
    return DS_OK;
}

ds_status_t heap_leftist_peek_min(const heap_leftist_t* h, ds_entry_t* out) {
    if (!h || !out) return DS_ERR_INVALID;
    if (!h->root) return DS_ERR_EMPTY;
    out->key = h->root->key;
    out->val = h->root->val;
    return DS_OK;
}

/* POP-MIN: discard the root (which holds the minimum) and meld its two subtrees
 * into the new root. Again everything reduces to meld. */
ds_status_t heap_leftist_pop_min(heap_leftist_t* h, ds_entry_t* out) {
    if (!h) return DS_ERR_INVALID;
    if (!h->root) return DS_ERR_EMPTY;
    lnode* r = h->root;
    if (out) { out->key = r->key; out->val = r->val; }
    h->root = lmerge(r->left, r->right);
    free(r);
    h->n--;
    return DS_OK;
}

ds_status_t heap_leftist_merge(heap_leftist_t* dst, heap_leftist_t* src) {
    if (!dst || !src) return DS_ERR_INVALID;
    dst->root = lmerge(dst->root, src->root);
    dst->n += src->n;
    src->root = NULL;
    src->n = 0;
    return DS_OK;
}
