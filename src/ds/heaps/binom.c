/* Binomial heap: forest of binomial trees, roots in ascending degree order. */
#include "dispatch/heaps.h"
#include <stdlib.h>

/* BINOMIAL TREE NODE: a binomial heap is a forest of binomial trees Bk where
 * Bk has exactly 2^k nodes. Each node tracks its degree (rank), child list head,
 * and right-sibling pointer; siblings let us walk the root list / child list. */
typedef struct bnode {
    ds_key_t key;
    ds_val_t val;
    int degree;
    struct bnode *parent, *child, *sibling;
} bnode;

struct heap_binom {
    bnode* head;
    size_t n;
};

heap_binom_t* heap_binom_create(void) {
    return (heap_binom_t*)calloc(1, sizeof(heap_binom_t));
}

static void bnode_free(bnode* x) {
    while (x) {
        bnode* sib = x->sibling;
        bnode_free(x->child);
        free(x);
        x = sib;
    }
}

void heap_binom_destroy(heap_binom_t* h) {
    if (!h) return;
    bnode_free(h->head);
    free(h);
}

size_t heap_binom_size(const heap_binom_t* h) { return h ? h->n : 0; }

/* LINK OPERATION: combines two binomial trees of equal rank k into one of rank k+1
 * by hanging the larger-key root y as a new child of the smaller-key root z. This
 * is the atomic step that keeps the heap-order invariant during a meld. */
static void binom_link(bnode* y, bnode* z) {
    y->parent = z;
    y->sibling = z->child;
    z->child = y;
    z->degree++;
}

static bnode* binom_merge_lists(bnode* a, bnode* b) {
    bnode head; head.sibling = NULL;
    bnode* tail = &head;
    while (a && b) {
        if (a->degree <= b->degree) { tail->sibling = a; a = a->sibling; }
        else                         { tail->sibling = b; b = b->sibling; }
        tail = tail->sibling;
    }
    tail->sibling = a ? a : b;
    return head.sibling;
}

/* MELD (UNION): the core operation. Merges the two root lists by ascending degree
 * and then walks the list collapsing any pair of equal-rank trees via binom_link
 * — analogous to binary addition with carries. Runs in O(log n). */
static bnode* binom_union(bnode* h1, bnode* h2) {
    bnode* h = binom_merge_lists(h1, h2);
    if (!h) return NULL;
    bnode* prev = NULL;
    bnode* x = h;
    bnode* next = x->sibling;
    while (next) {
        if (x->degree != next->degree ||
            (next->sibling && next->sibling->degree == x->degree)) {
            prev = x;
            x = next;
        } else if (x->key <= next->key) {
            x->sibling = next->sibling;
            binom_link(next, x);
        } else {
            if (!prev) h = next; else prev->sibling = next;
            binom_link(x, next);
            x = next;
        }
        next = x->sibling;
    }
    return h;
}

/* INSERT: build a singleton rank-0 binomial tree and meld it into the existing
 * forest. Amortised O(1); worst case O(log n). Used to enqueue new incidents. */
ds_status_t heap_binom_push(heap_binom_t* h, ds_key_t key, ds_val_t val) {
    if (!h) return DS_ERR_INVALID;
    bnode* n = (bnode*)calloc(1, sizeof(*n));
    if (!n) return DS_ERR_NOMEM;
    n->key = key;
    n->val = val;
    h->head = binom_union(h->head, n);
    h->n++;
    return DS_OK;
}

ds_status_t heap_binom_peek_min(const heap_binom_t* h, ds_entry_t* out) {
    if (!h || !out) return DS_ERR_INVALID;
    if (!h->head) return DS_ERR_EMPTY;
    bnode* min = h->head;
    for (bnode* x = h->head->sibling; x; x = x->sibling)
        if (x->key < min->key) min = x;
    out->key = min->key;
    out->val = min->val;
    return DS_OK;
}

/* EXTRACT-MIN: scans the root list to find the smallest root, detaches it,
 * reverses its child list (children are stored in descending rank), and melds
 * those children back into the main forest. O(log n). */
ds_status_t heap_binom_pop_min(heap_binom_t* h, ds_entry_t* out) {
    if (!h) return DS_ERR_INVALID;
    if (!h->head) return DS_ERR_EMPTY;
    bnode *min = h->head, *min_prev = NULL;
    bnode *prev = NULL, *x = h->head;
    while (x) {
        if (x->key < min->key) { min = x; min_prev = prev; }
        prev = x;
        x = x->sibling;
    }
    if (min_prev) min_prev->sibling = min->sibling;
    else          h->head = min->sibling;

    /* reverse child list */
    bnode* child = min->child;
    bnode* rev = NULL;
    while (child) {
        bnode* nx = child->sibling;
        child->parent = NULL;
        child->sibling = rev;
        rev = child;
        child = nx;
    }
    h->head = binom_union(h->head, rev);
    if (out) { out->key = min->key; out->val = min->val; }
    free(min);
    h->n--;
    return DS_OK;
}

ds_status_t heap_binom_merge(heap_binom_t* dst, heap_binom_t* src) {
    if (!dst || !src) return DS_ERR_INVALID;
    dst->head = binom_union(dst->head, src->head);
    dst->n += src->n;
    src->head = NULL;
    src->n = 0;
    return DS_OK;
}
