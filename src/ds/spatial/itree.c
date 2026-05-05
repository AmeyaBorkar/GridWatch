/* itree.c — augmented BST interval tree; unbalanced but fine for this project. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>

/* INTERVAL TREE NODE: stores an interval [lo, hi] keyed on lo (BST order),
 * AUGMENTED with max — the largest hi anywhere in this subtree. The max
 * augmentation is what makes stabbing queries efficient: a subtree can be
 * pruned whenever the query point exceeds its max. Used to track unit
 * shifts so we can ask "which units are on duty at time NOW?". */
typedef struct it_node {
    double lo, hi;
    double max;
    ds_val_t data;
    struct it_node* l;
    struct it_node* r;
} it_node_t;

typedef struct sp_itree {
    it_node_t* root;
} sp_itree_t;

sp_itree_t* sp_itree_create(void) {
    sp_itree_t* t = (sp_itree_t*)calloc(1, sizeof(sp_itree_t));
    return t;
}

static void it_free(it_node_t* n) {
    if (!n) return;
    it_free(n->l);
    it_free(n->r);
    free(n);
}

void sp_itree_destroy(sp_itree_t* t) {
    if (!t) return;
    it_free(t->root);
    free(t);
}

static it_node_t* it_insert(it_node_t* node, double lo, double hi, ds_val_t v, int* ok) {
    if (!node) {
        it_node_t* nn = (it_node_t*)malloc(sizeof(it_node_t));
        if (!nn) { *ok = 0; return NULL; }
        nn->lo = lo; nn->hi = hi; nn->max = hi;
        nn->data = v;
        nn->l = nn->r = NULL;
        return nn;
    }
    if (lo < node->lo) {
        node->l = it_insert(node->l, lo, hi, v, ok);
    } else {
        node->r = it_insert(node->r, lo, hi, v, ok);
    }
    if (hi > node->max) node->max = hi;
    return node;
}

ds_status_t sp_itree_insert(sp_itree_t* t, double lo, double hi, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    if (hi < lo) { double tmp = lo; lo = hi; hi = tmp; }
    int ok = 1;
    t->root = it_insert(t->root, lo, hi, v, &ok);
    return ok ? DS_OK : DS_ERR_NOMEM;
}

/* INTERVAL TREE STABBING QUERY: find every interval that contains the point p.
 * Uses the augmented subtree-max to PRUNE: if p > n->max no interval below n
 * can contain p. Otherwise we recurse left, test this node, and only recurse
 * right when p >= n->lo (since BST order is on lo, anything to the right
 * starts no earlier than n). This is how on-duty units are found. */
static void it_stab(const it_node_t* n, double p,
                     sp_rect_t* out, size_t max, size_t* cnt) {
    if (!n) return;
    if (p > n->max) return;
    if (n->l) it_stab(n->l, p, out, max, cnt);
    if (n->lo <= p && p <= n->hi) {
        if (*cnt < max) {
            out[*cnt].x1 = n->lo;
            out[*cnt].x2 = n->hi;
            out[*cnt].y1 = 0.0;
            out[*cnt].y2 = 0.0;
            out[*cnt].data = n->data;
        }
        (*cnt)++;
    }
    if (p < n->lo) return;
    if (n->r) it_stab(n->r, p, out, max, cnt);
}

size_t sp_itree_stab(const sp_itree_t* t, double p, sp_rect_t* out, size_t max) {
    if (!t) return 0;
    size_t cnt = 0;
    it_stab(t->root, p, out, max, &cnt);
    return cnt < max ? cnt : max;
}
