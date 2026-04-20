/* splay.c — top-down splay tree. */
#include "dispatch/trees.h"
#include <stdlib.h>

typedef struct splay_node {
    ds_key_t key;
    ds_val_t val;
    struct splay_node* left;
    struct splay_node* right;
} splay_node_t;

struct tree_splay {
    splay_node_t* root;
    size_t count;
};

static splay_node_t* rotate_right(splay_node_t* y) {
    splay_node_t* x = y->left;
    y->left = x->right;
    x->right = y;
    return x;
}

static splay_node_t* rotate_left(splay_node_t* x) {
    splay_node_t* y = x->right;
    x->right = y->left;
    y->left = x;
    return y;
}

/* Top-down splay. After this, the node with the closest key is root. */
static splay_node_t* splay(splay_node_t* root, ds_key_t key) {
    if (!root) return NULL;
    splay_node_t header;
    header.left = header.right = NULL;
    splay_node_t* l = &header;
    splay_node_t* r = &header;

    for (;;) {
        if (key < root->key) {
            if (!root->left) break;
            if (key < root->left->key) {
                root = rotate_right(root);
                if (!root->left) break;
            }
            r->left = root;
            r = root;
            root = root->left;
        } else if (key > root->key) {
            if (!root->right) break;
            if (key > root->right->key) {
                root = rotate_left(root);
                if (!root->right) break;
            }
            l->right = root;
            l = root;
            root = root->right;
        } else {
            break;
        }
    }
    l->right = root->left;
    r->left = root->right;
    root->left = header.right;
    root->right = header.left;
    return root;
}

static void destroy_rec(splay_node_t* n) {
    if (!n) return;
    destroy_rec(n->left);
    destroy_rec(n->right);
    free(n);
}

static void inorder_rec(const splay_node_t* n, ds_visitor_fn fn, void* user) {
    if (!n) return;
    inorder_rec(n->left, fn, user);
    fn(n->key, n->val, user);
    inorder_rec(n->right, fn, user);
}

tree_splay_t* tree_splay_create(void) {
    tree_splay_t* t = (tree_splay_t*)malloc(sizeof *t);
    if (!t) return NULL;
    t->root = NULL; t->count = 0;
    return t;
}

void tree_splay_destroy(tree_splay_t* t) {
    if (!t) return;
    destroy_rec(t->root);
    free(t);
}

ds_status_t tree_splay_insert(tree_splay_t* t, ds_key_t k, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    splay_node_t* n = (splay_node_t*)malloc(sizeof *n);
    if (!n) return DS_ERR_NOMEM;
    n->key = k; n->val = v; n->left = n->right = NULL;
    if (!t->root) { t->root = n; t->count = 1; return DS_OK; }
    t->root = splay(t->root, k);
    if (k < t->root->key) {
        n->left = t->root->left;
        n->right = t->root;
        t->root->left = NULL;
        t->root = n;
    } else if (k > t->root->key) {
        n->right = t->root->right;
        n->left = t->root;
        t->root->right = NULL;
        t->root = n;
    } else {
        free(n);
        return DS_ERR_DUP;
    }
    t->count++;
    return DS_OK;
}

ds_status_t tree_splay_get(tree_splay_t* t, ds_key_t k, ds_val_t* out) {
    if (!t || !t->root) return DS_ERR_NOT_FOUND;
    t->root = splay(t->root, k);
    if (t->root->key != k) return DS_ERR_NOT_FOUND;
    if (out) *out = t->root->val;
    return DS_OK;
}

ds_status_t tree_splay_delete(tree_splay_t* t, ds_key_t k) {
    if (!t || !t->root) return DS_ERR_NOT_FOUND;
    t->root = splay(t->root, k);
    if (t->root->key != k) return DS_ERR_NOT_FOUND;
    splay_node_t* dead = t->root;
    if (!dead->left) {
        t->root = dead->right;
    } else {
        splay_node_t* new_root = splay(dead->left, k);
        new_root->right = dead->right;
        t->root = new_root;
    }
    free(dead);
    t->count--;
    return DS_OK;
}

size_t tree_splay_size(const tree_splay_t* t) { return t ? t->count : 0; }

void tree_splay_inorder(const tree_splay_t* t, ds_visitor_fn fn, void* user) {
    if (!t || !fn) return;
    inorder_rec(t->root, fn, user);
}
