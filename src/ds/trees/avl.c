/* avl.c — AVL height-balanced BST. */
#include "dispatch/trees.h"
#include <stdlib.h>

/* The below block uses an AVL TREE NODE with an explicit height field per node.
 * Storing the height lets us compute the balance factor in O(1) during inserts/
 * deletes so we can detect imbalance and rotate. Used to key the unit roster by
 * unit id for O(log n) lookup. */
typedef struct avl_node {
    ds_key_t key;
    ds_val_t val;
    int height;
    struct avl_node* left;
    struct avl_node* right;
} avl_node_t;

struct tree_avl {
    avl_node_t* root;
    size_t count;
};

static int node_height(const avl_node_t* n) { return n ? n->height : 0; }

static int imax(int a, int b) { return a > b ? a : b; }

static void update_height(avl_node_t* n) {
    n->height = 1 + imax(node_height(n->left), node_height(n->right));
}

static int balance_factor(const avl_node_t* n) {
    return node_height(n->left) - node_height(n->right);
}

/* AVL ROTATION (right): single rotation that fixes a left-heavy imbalance by
 * pulling the left child up to become the new subtree root. Preserves BST
 * order while reducing height by one. */
static avl_node_t* rotate_right(avl_node_t* y) {
    avl_node_t* x = y->left;
    y->left = x->right;
    x->right = y;
    update_height(y);
    update_height(x);
    return x;
}

/* AVL ROTATION (left): mirror of rotate_right; fixes a right-heavy imbalance
 * by pulling the right child up. Together with rotate_right these are the only
 * structural primitives used to rebalance the tree. */
static avl_node_t* rotate_left(avl_node_t* x) {
    avl_node_t* y = x->right;
    x->right = y->left;
    y->left = x;
    update_height(x);
    update_height(y);
    return y;
}

/* AVL REBALANCE with single + DOUBLE ROTATIONS: inspects the balance factor
 * and applies left-right or right-left double rotations when the imbalanced
 * child leans the opposite way. Keeps tree height O(log n) so unit lookups
 * stay logarithmic. */
static avl_node_t* rebalance(avl_node_t* n) {
    update_height(n);
    int bf = balance_factor(n);
    if (bf > 1) {
        if (balance_factor(n->left) < 0) n->left = rotate_left(n->left);
        return rotate_right(n);
    }
    if (bf < -1) {
        if (balance_factor(n->right) > 0) n->right = rotate_right(n->right);
        return rotate_left(n);
    }
    return n;
}

static avl_node_t* node_new(ds_key_t k, ds_val_t v) {
    avl_node_t* n = (avl_node_t*)malloc(sizeof *n);
    if (!n) return NULL;
    n->key = k; n->val = v; n->height = 1; n->left = n->right = NULL;
    return n;
}

/* AVL INSERT WITH REBALANCE: standard BST insert, then on the way back up the
 * recursion stack each ancestor calls rebalance() so any node that became
 * unbalanced by this insert is fixed in one pass. */
static avl_node_t* insert_rec(avl_node_t* n, ds_key_t k, ds_val_t v,
                              ds_status_t* st) {
    if (!n) {
        avl_node_t* fresh = node_new(k, v);
        if (!fresh) { *st = DS_ERR_NOMEM; return NULL; }
        *st = DS_OK;
        return fresh;
    }
    if (k < n->key)      n->left  = insert_rec(n->left,  k, v, st);
    else if (k > n->key) n->right = insert_rec(n->right, k, v, st);
    else { *st = DS_ERR_DUP; return n; }
    if (*st != DS_OK) return n;
    return rebalance(n);
}

static avl_node_t* min_node(avl_node_t* n) {
    while (n->left) n = n->left;
    return n;
}

/* AVL DELETE WITH REBALANCE: classic BST deletion (with in-order successor
 * replacement for two-child nodes), then rebalance every ancestor on the
 * unwind to maintain the AVL height invariant. Lets units leave the roster
 * without degrading lookup performance. */
static avl_node_t* delete_rec(avl_node_t* n, ds_key_t k, ds_status_t* st) {
    if (!n) { *st = DS_ERR_NOT_FOUND; return NULL; }
    if (k < n->key)      n->left  = delete_rec(n->left,  k, st);
    else if (k > n->key) n->right = delete_rec(n->right, k, st);
    else {
        *st = DS_OK;
        if (!n->left || !n->right) {
            avl_node_t* child = n->left ? n->left : n->right;
            free(n);
            return child;
        }
        avl_node_t* succ = min_node(n->right);
        n->key = succ->key; n->val = succ->val;
        ds_status_t inner;
        n->right = delete_rec(n->right, succ->key, &inner);
        (void)inner;
    }
    if (!n) return NULL;
    return rebalance(n);
}

static void destroy_rec(avl_node_t* n) {
    if (!n) return;
    destroy_rec(n->left);
    destroy_rec(n->right);
    free(n);
}

static void inorder_rec(const avl_node_t* n, ds_visitor_fn fn, void* user) {
    if (!n) return;
    inorder_rec(n->left, fn, user);
    fn(n->key, n->val, user);
    inorder_rec(n->right, fn, user);
}

tree_avl_t* tree_avl_create(void) {
    tree_avl_t* t = (tree_avl_t*)malloc(sizeof *t);
    if (!t) return NULL;
    t->root = NULL; t->count = 0;
    return t;
}

void tree_avl_destroy(tree_avl_t* t) {
    if (!t) return;
    destroy_rec(t->root);
    free(t);
}

ds_status_t tree_avl_insert(tree_avl_t* t, ds_key_t k, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    ds_status_t st = DS_OK;
    t->root = insert_rec(t->root, k, v, &st);
    if (st == DS_OK) t->count++;
    return st;
}

ds_status_t tree_avl_get(const tree_avl_t* t, ds_key_t k, ds_val_t* out) {
    if (!t) return DS_ERR_INVALID;
    const avl_node_t* cur = t->root;
    while (cur) {
        if (k < cur->key) cur = cur->left;
        else if (k > cur->key) cur = cur->right;
        else { if (out) *out = cur->val; return DS_OK; }
    }
    return DS_ERR_NOT_FOUND;
}

ds_status_t tree_avl_delete(tree_avl_t* t, ds_key_t k) {
    if (!t) return DS_ERR_INVALID;
    ds_status_t st = DS_OK;
    t->root = delete_rec(t->root, k, &st);
    if (st == DS_OK) t->count--;
    return st;
}

size_t tree_avl_size(const tree_avl_t* t) { return t ? t->count : 0; }

void tree_avl_inorder(const tree_avl_t* t, ds_visitor_fn fn, void* user) {
    if (!t || !fn) return;
    inorder_rec(t->root, fn, user);
}
