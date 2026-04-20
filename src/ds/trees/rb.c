/* rb.c — Red-Black tree (CLRS). Uses a sentinel nil node. */
#include "dispatch/trees.h"
#include <stdlib.h>

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
