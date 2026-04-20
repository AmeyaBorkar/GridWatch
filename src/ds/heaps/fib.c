/* Fibonacci heap: lazy consolidation on extract-min; decrease-key via cut+cascade. */
#include "dispatch/heaps.h"
#include <stdlib.h>
#include <math.h>

struct heap_fib_node {
    ds_key_t key;
    ds_val_t val;
    struct heap_fib_node *parent, *child, *left, *right;
    int degree;
    int mark;
};

struct heap_fib {
    heap_fib_node_t* min;
    size_t n;
};

heap_fib_t* heap_fib_create(void) {
    heap_fib_t* h = (heap_fib_t*)calloc(1, sizeof(*h));
    return h;
}

static void fib_free_tree(heap_fib_node_t* x) {
    if (!x) return;
    heap_fib_node_t* cur = x;
    do {
        heap_fib_node_t* next = cur->right;
        if (cur->child) fib_free_tree(cur->child);
        free(cur);
        cur = next;
    } while (cur != x);
}

void heap_fib_destroy(heap_fib_t* h) {
    if (!h) return;
    if (h->min) fib_free_tree(h->min);
    free(h);
}

size_t heap_fib_size(const heap_fib_t* h) { return h ? h->n : 0; }

static void fib_list_insert(heap_fib_node_t** list, heap_fib_node_t* x) {
    if (!*list) {
        x->left = x->right = x;
        *list = x;
    } else {
        x->left = *list;
        x->right = (*list)->right;
        (*list)->right->left = x;
        (*list)->right = x;
    }
}

heap_fib_node_t* heap_fib_push(heap_fib_t* h, ds_key_t key, ds_val_t val) {
    if (!h) return NULL;
    heap_fib_node_t* n = (heap_fib_node_t*)calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->key = key;
    n->val = val;
    n->left = n->right = n;
    fib_list_insert(&h->min, n);
    if (key < h->min->key) h->min = n;
    h->n++;
    return n;
}

ds_status_t heap_fib_peek_min(const heap_fib_t* h, ds_entry_t* out) {
    if (!h || !out) return DS_ERR_INVALID;
    if (!h->min) return DS_ERR_EMPTY;
    out->key = h->min->key;
    out->val = h->min->val;
    return DS_OK;
}

static void fib_link(heap_fib_node_t* y, heap_fib_node_t* x) {
    /* remove y from root list */
    y->left->right = y->right;
    y->right->left = y->left;
    y->parent = x;
    if (!x->child) {
        x->child = y;
        y->left = y->right = y;
    } else {
        y->left = x->child;
        y->right = x->child->right;
        x->child->right->left = y;
        x->child->right = y;
    }
    x->degree++;
    y->mark = 0;
}

static ds_status_t fib_consolidate(heap_fib_t* h) {
    if (!h->min) return DS_OK;
    int D = (int)(log2((double)h->n) * 2) + 2;
    heap_fib_node_t** A = (heap_fib_node_t**)calloc((size_t)D, sizeof(*A));
    if (!A) return DS_ERR_NOMEM;

    /* collect root list */
    size_t cap = 16, cnt = 0;
    heap_fib_node_t** roots = (heap_fib_node_t**)malloc(cap * sizeof(*roots));
    if (!roots) { free(A); return DS_ERR_NOMEM; }
    heap_fib_node_t* w = h->min;
    do {
        if (cnt == cap) {
            cap *= 2;
            heap_fib_node_t** tmp = (heap_fib_node_t**)realloc(roots, cap * sizeof(*roots));
            if (!tmp) { free(roots); free(A); return DS_ERR_NOMEM; }
            roots = tmp;
        }
        roots[cnt++] = w;
        w = w->right;
    } while (w != h->min);

    for (size_t i = 0; i < cnt; i++) {
        heap_fib_node_t* x = roots[i];
        int d = x->degree;
        while (d < D && A[d]) {
            heap_fib_node_t* y = A[d];
            if (y->key < x->key) { heap_fib_node_t* t = x; x = y; y = t; }
            fib_link(y, x);
            A[d] = NULL;
            d++;
        }
        if (d < D) A[d] = x;
    }
    free(roots);

    h->min = NULL;
    for (int i = 0; i < D; i++) {
        if (A[i]) {
            A[i]->left = A[i]->right = A[i];
            A[i]->parent = NULL;
            fib_list_insert(&h->min, A[i]);
            if (A[i]->key < h->min->key) h->min = A[i];
        }
    }
    free(A);
    return DS_OK;
}

ds_status_t heap_fib_pop_min(heap_fib_t* h, ds_entry_t* out) {
    if (!h) return DS_ERR_INVALID;
    heap_fib_node_t* z = h->min;
    if (!z) return DS_ERR_EMPTY;
    if (out) { out->key = z->key; out->val = z->val; }

    /* add children to root list */
    if (z->child) {
        heap_fib_node_t* c = z->child;
        do {
            c->parent = NULL;
            c = c->right;
        } while (c != z->child);
        /* splice child list into root list */
        heap_fib_node_t* zr = z->right;
        heap_fib_node_t* cl = z->child->left;
        z->right = z->child;
        z->child->left = z;
        cl->right = zr;
        zr->left = cl;
    }
    /* remove z */
    z->left->right = z->right;
    z->right->left = z->left;
    if (z == z->right) {
        h->min = NULL;
    } else {
        h->min = z->right;
        ds_status_t st = fib_consolidate(h);
        if (st != DS_OK) { free(z); return st; }
    }
    free(z);
    h->n--;
    return DS_OK;
}

static void fib_cut(heap_fib_t* h, heap_fib_node_t* x, heap_fib_node_t* y) {
    if (x->right == x) {
        y->child = NULL;
    } else {
        x->left->right = x->right;
        x->right->left = x->left;
        if (y->child == x) y->child = x->right;
    }
    y->degree--;
    x->left = x->right = x;
    x->parent = NULL;
    x->mark = 0;
    fib_list_insert(&h->min, x);
}

static void fib_cascading_cut(heap_fib_t* h, heap_fib_node_t* y) {
    heap_fib_node_t* z = y->parent;
    if (z) {
        if (!y->mark) {
            y->mark = 1;
        } else {
            fib_cut(h, y, z);
            fib_cascading_cut(h, z);
        }
    }
}

ds_status_t heap_fib_decrease_key(heap_fib_t* h, heap_fib_node_t* x, ds_key_t new_key) {
    if (!h || !x) return DS_ERR_INVALID;
    if (new_key > x->key) return DS_ERR_INVALID;
    x->key = new_key;
    heap_fib_node_t* y = x->parent;
    if (y && x->key < y->key) {
        fib_cut(h, x, y);
        fib_cascading_cut(h, y);
    }
    if (x->key < h->min->key) h->min = x;
    return DS_OK;
}
