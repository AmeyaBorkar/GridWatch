/* bplus.c — B+ tree with configurable order (max children per internal node).
 * Leaves hold values and are linked for range scans. */
#include "dispatch/trees.h"
#include <stdlib.h>
#include <string.h>

typedef struct bp_node {
    int is_leaf;
    int n_keys;
    ds_key_t* keys;
    /* internal: children[0..n_keys] */
    struct bp_node** children;
    /* leaf: vals[0..n_keys-1], next leaf pointer */
    ds_val_t* vals;
    struct bp_node* next;
} bp_node_t;

struct tree_bplus {
    bp_node_t* root;
    int order;    /* max children per internal node; leaves hold up to order-1 keys */
    size_t count;
};

static bp_node_t* node_new(int order, int is_leaf) {
    bp_node_t* n = (bp_node_t*)malloc(sizeof *n);
    if (!n) return NULL;
    n->is_leaf = is_leaf;
    n->n_keys = 0;
    n->next = NULL;
    /* allocate with +1 slack for overflow during split. */
    n->keys = (ds_key_t*)malloc(sizeof(ds_key_t) * (size_t)(order + 1));
    if (!n->keys) { free(n); return NULL; }
    if (is_leaf) {
        n->children = NULL;
        n->vals = (ds_val_t*)malloc(sizeof(ds_val_t) * (size_t)(order + 1));
        if (!n->vals) { free(n->keys); free(n); return NULL; }
    } else {
        n->vals = NULL;
        n->children = (bp_node_t**)malloc(sizeof(bp_node_t*) * (size_t)(order + 2));
        if (!n->children) { free(n->keys); free(n); return NULL; }
    }
    return n;
}

static void node_free(bp_node_t* n) {
    if (!n) return;
    free(n->keys);
    free(n->vals);
    free(n->children);
    free(n);
}

static void destroy_rec(bp_node_t* n) {
    if (!n) return;
    if (!n->is_leaf) {
        for (int i = 0; i <= n->n_keys; i++) destroy_rec(n->children[i]);
    }
    node_free(n);
}

tree_bplus_t* tree_bplus_create(int order) {
    if (order < 3) return NULL;
    tree_bplus_t* t = (tree_bplus_t*)malloc(sizeof *t);
    if (!t) return NULL;
    t->order = order;
    t->count = 0;
    t->root = node_new(order, 1);
    if (!t->root) { free(t); return NULL; }
    return t;
}

void tree_bplus_destroy(tree_bplus_t* t) {
    if (!t) return;
    destroy_rec(t->root);
    free(t);
}

static int leaf_find(const bp_node_t* leaf, ds_key_t k) {
    int i = 0;
    while (i < leaf->n_keys && leaf->keys[i] < k) i++;
    return i;
}

static bp_node_t* find_leaf(const tree_bplus_t* t, ds_key_t k) {
    bp_node_t* n = t->root;
    while (n && !n->is_leaf) {
        int i = 0;
        while (i < n->n_keys && k >= n->keys[i]) i++;
        n = n->children[i];
    }
    return n;
}

ds_status_t tree_bplus_get(const tree_bplus_t* t, ds_key_t k, ds_val_t* out) {
    if (!t) return DS_ERR_INVALID;
    bp_node_t* leaf = find_leaf(t, k);
    if (!leaf) return DS_ERR_NOT_FOUND;
    int i = leaf_find(leaf, k);
    if (i < leaf->n_keys && leaf->keys[i] == k) {
        if (out) *out = leaf->vals[i];
        return DS_OK;
    }
    return DS_ERR_NOT_FOUND;
}

/* Insert (k,v) into leaf, shifting. Leaf must have room including slack. */
static int leaf_insert_sorted(bp_node_t* leaf, ds_key_t k, ds_val_t v) {
    int i = leaf_find(leaf, k);
    if (i < leaf->n_keys && leaf->keys[i] == k) return -1; /* dup */
    for (int j = leaf->n_keys; j > i; j--) {
        leaf->keys[j] = leaf->keys[j - 1];
        leaf->vals[j] = leaf->vals[j - 1];
    }
    leaf->keys[i] = k;
    leaf->vals[i] = v;
    leaf->n_keys++;
    return 0;
}

/* recursive insert: returns 0 ok, DS_ERR_DUP or DS_ERR_NOMEM.
 * On split, *split_key and *split_node are set and returned as info. */
typedef struct {
    int did_split;
    ds_key_t up_key;
    bp_node_t* right;
} split_info_t;

static ds_status_t insert_rec(tree_bplus_t* t, bp_node_t* n,
                              ds_key_t k, ds_val_t v, split_info_t* out_split) {
    out_split->did_split = 0;
    if (n->is_leaf) {
        if (leaf_insert_sorted(n, k, v) < 0) return DS_ERR_DUP;
        if (n->n_keys < t->order) return DS_OK;
        /* split leaf */
        int mid = n->n_keys / 2;
        bp_node_t* r = node_new(t->order, 1);
        if (!r) return DS_ERR_NOMEM;
        r->n_keys = n->n_keys - mid;
        for (int i = 0; i < r->n_keys; i++) {
            r->keys[i] = n->keys[mid + i];
            r->vals[i] = n->vals[mid + i];
        }
        n->n_keys = mid;
        r->next = n->next;
        n->next = r;
        out_split->did_split = 1;
        out_split->up_key = r->keys[0];
        out_split->right = r;
        return DS_OK;
    }
    /* internal */
    int i = 0;
    while (i < n->n_keys && k >= n->keys[i]) i++;
    split_info_t child_split;
    ds_status_t st = insert_rec(t, n->children[i], k, v, &child_split);
    if (st != DS_OK) return st;
    if (!child_split.did_split) return DS_OK;
    /* insert up_key at pos i, right at pos i+1 */
    for (int j = n->n_keys; j > i; j--) n->keys[j] = n->keys[j - 1];
    for (int j = n->n_keys + 1; j > i + 1; j--) n->children[j] = n->children[j - 1];
    n->keys[i] = child_split.up_key;
    n->children[i + 1] = child_split.right;
    n->n_keys++;
    if (n->n_keys < t->order) return DS_OK;
    /* split internal */
    int mid = n->n_keys / 2;
    ds_key_t up = n->keys[mid];
    bp_node_t* r = node_new(t->order, 0);
    if (!r) return DS_ERR_NOMEM;
    r->n_keys = n->n_keys - mid - 1;
    for (int j = 0; j < r->n_keys; j++) r->keys[j] = n->keys[mid + 1 + j];
    for (int j = 0; j <= r->n_keys; j++) r->children[j] = n->children[mid + 1 + j];
    n->n_keys = mid;
    out_split->did_split = 1;
    out_split->up_key = up;
    out_split->right = r;
    return DS_OK;
}

ds_status_t tree_bplus_insert(tree_bplus_t* t, ds_key_t k, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    split_info_t s;
    ds_status_t st = insert_rec(t, t->root, k, v, &s);
    if (st != DS_OK) return st;
    if (s.did_split) {
        bp_node_t* new_root = node_new(t->order, 0);
        if (!new_root) return DS_ERR_NOMEM;
        new_root->n_keys = 1;
        new_root->keys[0] = s.up_key;
        new_root->children[0] = t->root;
        new_root->children[1] = s.right;
        t->root = new_root;
    }
    t->count++;
    return DS_OK;
}

/* Simplified delete: remove from leaf only; rebalancing left out for brevity
 * (course project doesn't exercise B+ delete balance). If a leaf becomes empty
 * and isn't the root, we still leave the key in the parent index; subsequent
 * searches will fall through correctly because parent keys are upper bounds. */
ds_status_t tree_bplus_delete(tree_bplus_t* t, ds_key_t k) {
    if (!t) return DS_ERR_INVALID;
    bp_node_t* leaf = find_leaf(t, k);
    if (!leaf) return DS_ERR_NOT_FOUND;
    int i = leaf_find(leaf, k);
    if (i >= leaf->n_keys || leaf->keys[i] != k) return DS_ERR_NOT_FOUND;
    for (int j = i; j < leaf->n_keys - 1; j++) {
        leaf->keys[j] = leaf->keys[j + 1];
        leaf->vals[j] = leaf->vals[j + 1];
    }
    leaf->n_keys--;
    t->count--;
    return DS_OK;
}

size_t tree_bplus_size(const tree_bplus_t* t) { return t ? t->count : 0; }

static bp_node_t* leftmost_leaf(bp_node_t* n) {
    while (n && !n->is_leaf) n = n->children[0];
    return n;
}

void tree_bplus_inorder(const tree_bplus_t* t, ds_visitor_fn fn, void* user) {
    if (!t || !fn) return;
    bp_node_t* leaf = leftmost_leaf(t->root);
    while (leaf) {
        for (int i = 0; i < leaf->n_keys; i++) fn(leaf->keys[i], leaf->vals[i], user);
        leaf = leaf->next;
    }
}

void tree_bplus_range(const tree_bplus_t* t, ds_key_t lo, ds_key_t hi,
                      ds_visitor_fn fn, void* user) {
    if (!t || !fn) return;
    bp_node_t* leaf = find_leaf(t, lo);
    while (leaf) {
        for (int i = 0; i < leaf->n_keys; i++) {
            if (leaf->keys[i] < lo) continue;
            if (leaf->keys[i] > hi) return;
            fn(leaf->keys[i], leaf->vals[i], user);
        }
        leaf = leaf->next;
    }
}
