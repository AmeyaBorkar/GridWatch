/* rb.c — Red-Black tree (CLRS). Uses a sentinel nil node. */
#include "dispatch/trees.h"
#include <stdlib.h>

/* RED-BLACK TREE NODE: each node carries a color (RED or BLACK) and a parent
 * pointer. The color invariants (root is black, no two reds in a row, every
 * root->NIL path has equal black-count) bound the tree height to ~2 log n.
 * Used as the pending-incident index keyed by spawn time. */
typedef enum { RB_RED = 0, RB_BLACK = 1 } rb_color_t;

typedef struct rb_node {
    ds_key_t key;
    ds_val_t val;
    rb_color_t color;
    struct rb_node* left;
    struct rb_node* right;
    struct rb_node* parent;
} rb_node_t;

struct tree_rb {
    rb_node_t* root;
    rb_node_t* nil; /* sentinel */
    size_t count;
};

static rb_node_t* node_new(tree_rb_t* t, ds_key_t k, ds_val_t v) {
    rb_node_t* n = (rb_node_t*)malloc(sizeof *n);
    if (!n) return NULL;
    n->key = k; n->val = v; n->color = RB_RED;
    n->left = t->nil; n->right = t->nil; n->parent = t->nil;
    return n;
}

/* RB ROTATION (left): structural primitive used by both insert_fixup and
 * delete_fixup to rebuild the tree shape while preserving BST order. */
static void rotate_left(tree_rb_t* t, rb_node_t* x) {
    rb_node_t* y = x->right;
    x->right = y->left;
    if (y->left != t->nil) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == t->nil) t->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

/* RB ROTATION (right): mirror of rotate_left, also used by both fixups. */
static void rotate_right(tree_rb_t* t, rb_node_t* y) {
    rb_node_t* x = y->left;
    y->left = x->right;
    if (x->right != t->nil) x->right->parent = y;
    x->parent = y->parent;
    if (y->parent == t->nil) t->root = x;
    else if (y == y->parent->left) y->parent->left = x;
    else y->parent->right = x;
    x->right = y;
    y->parent = x;
}

/* RB INSERT FIXUP: the new node z is colored RED; this loop walks up the
 * tree RECOLORING (uncle red case) or ROTATING (uncle black case) until the
 * red-red violation is gone, finally repainting the root black. Restores all
 * RB invariants without rebuilding the tree. */
static void insert_fixup(tree_rb_t* t, rb_node_t* z) {
    while (z->parent->color == RB_RED) {
        if (z->parent == z->parent->parent->left) {
            rb_node_t* y = z->parent->parent->right;
            if (y->color == RB_RED) {
                z->parent->color = RB_BLACK;
                y->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) { z = z->parent; rotate_left(t, z); }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rotate_right(t, z->parent->parent);
            }
        } else {
            rb_node_t* y = z->parent->parent->left;
            if (y->color == RB_RED) {
                z->parent->color = RB_BLACK;
                y->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) { z = z->parent; rotate_right(t, z); }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rotate_left(t, z->parent->parent);
            }
        }
    }
    t->root->color = RB_BLACK;
}

static void transplant(tree_rb_t* t, rb_node_t* u, rb_node_t* v) {
    if (u->parent == t->nil) t->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;
}

static rb_node_t* tree_min(tree_rb_t* t, rb_node_t* n) {
    while (n->left != t->nil) n = n->left;
    return n;
}

/* RB DELETE FIXUP: when removing a black node breaks the equal-black-count
 * invariant, x carries an extra "double black" charge. The four CLRS cases
 * (sibling-red, sibling-black with various nephew colors) recolor and rotate
 * to discharge it back to the root or absorb it into a red node. */
static void delete_fixup(tree_rb_t* t, rb_node_t* x) {
    while (x != t->root && x->color == RB_BLACK) {
        if (x == x->parent->left) {
            rb_node_t* w = x->parent->right;
            if (w->color == RB_RED) {
                w->color = RB_BLACK;
                x->parent->color = RB_RED;
                rotate_left(t, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == RB_BLACK && w->right->color == RB_BLACK) {
                w->color = RB_RED;
                x = x->parent;
            } else {
                if (w->right->color == RB_BLACK) {
                    w->left->color = RB_BLACK;
                    w->color = RB_RED;
                    rotate_right(t, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = RB_BLACK;
                w->right->color = RB_BLACK;
                rotate_left(t, x->parent);
                x = t->root;
            }
        } else {
            rb_node_t* w = x->parent->left;
            if (w->color == RB_RED) {
                w->color = RB_BLACK;
                x->parent->color = RB_RED;
                rotate_right(t, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == RB_BLACK && w->left->color == RB_BLACK) {
                w->color = RB_RED;
                x = x->parent;
            } else {
                if (w->left->color == RB_BLACK) {
                    w->right->color = RB_BLACK;
                    w->color = RB_RED;
                    rotate_left(t, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = RB_BLACK;
                w->left->color = RB_BLACK;
                rotate_right(t, x->parent);
                x = t->root;
            }
        }
    }
    x->color = RB_BLACK;
}

static void destroy_rec(tree_rb_t* t, rb_node_t* n) {
    if (n == t->nil) return;
    destroy_rec(t, n->left);
    destroy_rec(t, n->right);
    free(n);
}

static void inorder_rec(const tree_rb_t* t, const rb_node_t* n,
                        ds_visitor_fn fn, void* user) {
    if (n == t->nil) return;
    inorder_rec(t, n->left, fn, user);
    fn(n->key, n->val, user);
    inorder_rec(t, n->right, fn, user);
}

tree_rb_t* tree_rb_create(void) {
    tree_rb_t* t = (tree_rb_t*)malloc(sizeof *t);
    if (!t) return NULL;
    t->nil = (rb_node_t*)malloc(sizeof *t->nil);
    if (!t->nil) { free(t); return NULL; }
    t->nil->color = RB_BLACK;
    t->nil->left = t->nil->right = t->nil->parent = t->nil;
    t->nil->key = 0; t->nil->val = NULL;
    t->root = t->nil;
    t->count = 0;
    return t;
}

void tree_rb_destroy(tree_rb_t* t) {
    if (!t) return;
    destroy_rec(t, t->root);
    free(t->nil);
    free(t);
}

/* RB INSERT: standard BST descent to find the slot, link the new red node,
 * then call insert_fixup to restore color invariants. Used to insert pending
 * incidents keyed by spawn time so the earliest pending incident is always
 * findable in O(log n). */
ds_status_t tree_rb_insert(tree_rb_t* t, ds_key_t k, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    rb_node_t* y = t->nil;
    rb_node_t* x = t->root;
    while (x != t->nil) {
        y = x;
        if (k < x->key) x = x->left;
        else if (k > x->key) x = x->right;
        else return DS_ERR_DUP;
    }
    rb_node_t* z = node_new(t, k, v);
    if (!z) return DS_ERR_NOMEM;
    z->parent = y;
    if (y == t->nil) t->root = z;
    else if (k < y->key) y->left = z;
    else y->right = z;
    insert_fixup(t, z);
    t->count++;
    return DS_OK;
}

ds_status_t tree_rb_get(const tree_rb_t* t, ds_key_t k, ds_val_t* out) {
    if (!t) return DS_ERR_INVALID;
    const rb_node_t* cur = t->root;
    while (cur != t->nil) {
        if (k < cur->key) cur = cur->left;
        else if (k > cur->key) cur = cur->right;
        else { if (out) *out = cur->val; return DS_OK; }
    }
    return DS_ERR_NOT_FOUND;
}

/* RB DELETE: locate node, splice it out using transplant (with in-order
 * successor for two-child case), and if a black node was removed call
 * delete_fixup to restore the color invariants. Removes resolved incidents
 * from the pending index. */
ds_status_t tree_rb_delete(tree_rb_t* t, ds_key_t k) {
    if (!t) return DS_ERR_INVALID;
    rb_node_t* z = t->root;
    while (z != t->nil && z->key != k) {
        z = (k < z->key) ? z->left : z->right;
    }
    if (z == t->nil) return DS_ERR_NOT_FOUND;

    rb_node_t* y = z;
    rb_node_t* x;
    rb_color_t y_orig = y->color;
    if (z->left == t->nil) {
        x = z->right;
        transplant(t, z, z->right);
    } else if (z->right == t->nil) {
        x = z->left;
        transplant(t, z, z->left);
    } else {
        y = tree_min(t, z->right);
        y_orig = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }
    free(z);
    if (y_orig == RB_BLACK) delete_fixup(t, x);
    t->count--;
    return DS_OK;
}

size_t tree_rb_size(const tree_rb_t* t) { return t ? t->count : 0; }

void tree_rb_inorder(const tree_rb_t* t, ds_visitor_fn fn, void* user) {
    if (!t || !fn) return;
    inorder_rec(t, t->root, fn, user);
}
