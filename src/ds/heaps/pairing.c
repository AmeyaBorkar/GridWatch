/* Pairing heap: multiway tree; delete-min via two-pass pairing merge of children. */
#include "dispatch/heaps.h"
#include <stdlib.h>

typedef struct pnode {
    ds_key_t key;
    ds_val_t val;
    struct pnode *child, *sibling;
} pnode;

struct heap_pairing {
    pnode* root;
    size_t n;
};

heap_pairing_t* heap_pairing_create(void) {
    return (heap_pairing_t*)calloc(1, sizeof(heap_pairing_t));
}

static void pnode_free(pnode* x) {
    while (x) {
        pnode* sib = x->sibling;
        pnode_free(x->child);
        free(x);
        x = sib;
    }
}

void heap_pairing_destroy(heap_pairing_t* h) {
    if (!h) return;
    pnode_free(h->root);
    free(h);
}

size_t heap_pairing_size(const heap_pairing_t* h) { return h ? h->n : 0; }

static pnode* pmerge(pnode* a, pnode* b) {
    if (!a) return b;
    if (!b) return a;
    if (b->key < a->key) { pnode* t = a; a = b; b = t; }
    b->sibling = a->child;
    a->child = b;
    return a;
}

static pnode* pmerge_pairs(pnode* x) {
    if (!x || !x->sibling) return x;
    pnode* a = x;
    pnode* b = x->sibling;
    pnode* rest = b->sibling;
    a->sibling = NULL;
    b->sibling = NULL;
    return pmerge(pmerge(a, b), pmerge_pairs(rest));
}

ds_status_t heap_pairing_push(heap_pairing_t* h, ds_key_t key, ds_val_t val) {
    if (!h) return DS_ERR_INVALID;
    pnode* n = (pnode*)calloc(1, sizeof(*n));
    if (!n) return DS_ERR_NOMEM;
    n->key = key;
    n->val = val;
    h->root = pmerge(h->root, n);
    h->n++;
    return DS_OK;
}

ds_status_t heap_pairing_peek_min(const heap_pairing_t* h, ds_entry_t* out) {
    if (!h || !out) return DS_ERR_INVALID;
    if (!h->root) return DS_ERR_EMPTY;
    out->key = h->root->key;
    out->val = h->root->val;
    return DS_OK;
}

ds_status_t heap_pairing_pop_min(heap_pairing_t* h, ds_entry_t* out) {
    if (!h) return DS_ERR_INVALID;
    if (!h->root) return DS_ERR_EMPTY;
    pnode* r = h->root;
    if (out) { out->key = r->key; out->val = r->val; }
    h->root = pmerge_pairs(r->child);
    free(r);
    h->n--;
    return DS_OK;
}
