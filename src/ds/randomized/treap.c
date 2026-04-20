/* treap.c — BST with random priorities, split/merge implementation. */
#include <stdlib.h>
#include <stdint.h>
#include "dispatch/randomized.h"

uint64_t rnd__mix_u64(uint64_t* state);
uint64_t rnd__fresh_state(void);

typedef struct treap_node_s {
    ds_key_t key;
    ds_val_t val;
    uint64_t priority;
    struct treap_node_s* left;
    struct treap_node_s* right;
} treap_node_t;

struct rnd_treap_s {
    treap_node_t* root;
    size_t        size;
    uint64_t      rng;
};

/* Split `t` by key: left subtree has keys < k, right has keys >= k. */
static void treap_split(treap_node_t* t, ds_key_t k,
                        treap_node_t** lo, treap_node_t** hi) {
    if (!t) { *lo = NULL; *hi = NULL; return; }
    if (t->key < k) {
        treap_split(t->right, k, &t->right, hi);
        *lo = t;
    } else {
        treap_split(t->left, k, lo, &t->left);
        *hi = t;
    }
}

/* Merge two treaps where all keys in `l` < all keys in `r`. */
static treap_node_t* treap_merge(treap_node_t* l, treap_node_t* r) {
    if (!l) return r;
    if (!r) return l;
    if (l->priority > r->priority) {
        l->right = treap_merge(l->right, r);
        return l;
    } else {
        r->left = treap_merge(l, r->left);
        return r;
    }
}

static void treap_free(treap_node_t* n) {
    if (!n) return;
    treap_free(n->left);
    treap_free(n->right);
    free(n);
}

rnd_treap_t* rnd_treap_create(void) {
    rnd_treap_t* t = (rnd_treap_t*)malloc(sizeof(*t));
    if (!t) return NULL;
    t->root = NULL;
    t->size = 0;
    t->rng = rnd__fresh_state();
    return t;
}

void rnd_treap_destroy(rnd_treap_t* t) {
    if (!t) return;
    treap_free(t->root);
    free(t);
}

static treap_node_t* treap_find(treap_node_t* n, ds_key_t k) {
    while (n) {
        if (k < n->key) n = n->left;
        else if (n->key < k) n = n->right;
        else return n;
    }
    return NULL;
}

ds_status_t rnd_treap_insert(rnd_treap_t* t, ds_key_t k, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    treap_node_t* existing = treap_find(t->root, k);
    if (existing) { existing->val = v; return DS_OK; }

    treap_node_t* n = (treap_node_t*)malloc(sizeof(*n));
    if (!n) return DS_ERR_NOMEM;
    n->key = k;
    n->val = v;
    n->priority = rnd__mix_u64(&t->rng);
    n->left = NULL;
    n->right = NULL;

    treap_node_t* lo = NULL;
    treap_node_t* hi = NULL;
    treap_split(t->root, k, &lo, &hi);
    t->root = treap_merge(treap_merge(lo, n), hi);
    ++t->size;
    return DS_OK;
}

ds_status_t rnd_treap_get(const rnd_treap_t* t, ds_key_t k, ds_val_t* out) {
    if (!t || !out) return DS_ERR_INVALID;
    treap_node_t* n = treap_find(t->root, k);
    if (!n) return DS_ERR_NOT_FOUND;
    *out = n->val;
    return DS_OK;
}

ds_status_t rnd_treap_delete(rnd_treap_t* t, ds_key_t k) {
    if (!t) return DS_ERR_INVALID;
    /* Find node and its parent; splice by merging its two children. */
    treap_node_t* parent = NULL;
    treap_node_t* cur = t->root;
    int is_left_child = 0;
    while (cur && cur->key != k) {
        parent = cur;
        if (k < cur->key) { cur = cur->left; is_left_child = 1; }
        else              { cur = cur->right; is_left_child = 0; }
    }
    if (!cur) return DS_ERR_NOT_FOUND;
    treap_node_t* repl = treap_merge(cur->left, cur->right);
    if (!parent) t->root = repl;
    else if (is_left_child) parent->left = repl;
    else parent->right = repl;
    free(cur);
    --t->size;
    return DS_OK;
}

size_t rnd_treap_size(const rnd_treap_t* t) {
    return t ? t->size : 0;
}
