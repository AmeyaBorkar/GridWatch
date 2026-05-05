/* rtree.c — R-tree with linear split. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define RT_MAX 8

/* The below block uses the R-TREE node layout: each node packs up to RT_MAX
 * entries (rectangles + child pointer or data payload) plus a single MBR
 * (bx1..by2) covering them all. The MBR is what lets a search prune entire
 * subtrees that can't overlap the query — this is how station coverage
 * rectangles are indexed for fast overlap queries. */
typedef struct rt_node {
    int leaf;
    size_t n;
    double x1[RT_MAX+1], y1[RT_MAX+1], x2[RT_MAX+1], y2[RT_MAX+1];
    ds_val_t data[RT_MAX+1];
    struct rt_node* child[RT_MAX+1];
    double bx1, by1, bx2, by2;
} rt_node_t;

typedef struct sp_rtree {
    rt_node_t* root;
} sp_rtree_t;

static rt_node_t* rt_node_new(int leaf) {
    rt_node_t* n = (rt_node_t*)calloc(1, sizeof(rt_node_t));
    if (!n) return NULL;
    n->leaf = leaf;
    n->bx1 = DBL_MAX; n->by1 = DBL_MAX;
    n->bx2 = -DBL_MAX; n->by2 = -DBL_MAX;
    return n;
}

sp_rtree_t* sp_rtree_create(void) {
    sp_rtree_t* t = (sp_rtree_t*)calloc(1, sizeof(sp_rtree_t));
    if (!t) return NULL;
    t->root = rt_node_new(1);
    if (!t->root) { free(t); return NULL; }
    return t;
}

static void rt_free(rt_node_t* n) {
    if (!n) return;
    if (!n->leaf) {
        for (size_t i = 0; i < n->n; i++) rt_free(n->child[i]);
    }
    free(n);
}

void sp_rtree_destroy(sp_rtree_t* t) {
    if (!t) return;
    rt_free(t->root);
    free(t);
}

static double rt_area(double x1, double y1, double x2, double y2) {
    return (x2 - x1) * (y2 - y1);
}

static void mbr_expand(double* bx1, double* by1, double* bx2, double* by2,
                        double x1, double y1, double x2, double y2) {
    if (x1 < *bx1) *bx1 = x1;
    if (y1 < *by1) *by1 = y1;
    if (x2 > *bx2) *bx2 = x2;
    if (y2 > *by2) *by2 = y2;
}

static void recompute_mbr(rt_node_t* n) {
    n->bx1 = DBL_MAX; n->by1 = DBL_MAX;
    n->bx2 = -DBL_MAX; n->by2 = -DBL_MAX;
    for (size_t i = 0; i < n->n; i++) {
        mbr_expand(&n->bx1, &n->by1, &n->bx2, &n->by2,
                   n->x1[i], n->y1[i], n->x2[i], n->y2[i]);
    }
}

static double node_enlarge(const rt_node_t* n,
                            double x1, double y1, double x2, double y2) {
    double nx1 = n->bx1 < x1 ? n->bx1 : x1;
    double ny1 = n->by1 < y1 ? n->by1 : y1;
    double nx2 = n->bx2 > x2 ? n->bx2 : x2;
    double ny2 = n->by2 > y2 ? n->by2 : y2;
    double old = 0.0;
    if (n->bx2 >= n->bx1 && n->by2 >= n->by1)
        old = rt_area(n->bx1, n->by1, n->bx2, n->by2);
    return rt_area(nx1, ny1, nx2, ny2) - old;
}

/* The below block uses R-TREE LINEAR SPLIT: pick the two entries that are
 * farthest apart as seeds, then assign every other entry to whichever group
 * needs the smaller MBR enlargement to swallow it. This is what keeps the
 * tree balanced when a node overflows during insert. */
static void linear_split(rt_node_t* n, rt_node_t* nn) {
    size_t total = n->n;
    size_t seed1 = 0, seed2 = 1;
    double best = -DBL_MAX;
    for (size_t i = 0; i < total; i++) {
        for (size_t j = i + 1; j < total; j++) {
            double dx = n->x1[i] - n->x2[j]; if (dx < 0) dx = -dx;
            double dy = n->y1[i] - n->y2[j]; if (dy < 0) dy = -dy;
            double d = dx + dy;
            if (d > best) { best = d; seed1 = i; seed2 = j; }
        }
    }
    double tx1[RT_MAX+1], ty1[RT_MAX+1], tx2[RT_MAX+1], ty2[RT_MAX+1];
    ds_val_t td[RT_MAX+1];
    rt_node_t* tc[RT_MAX+1];
    for (size_t i = 0; i < total; i++) {
        tx1[i] = n->x1[i]; ty1[i] = n->y1[i];
        tx2[i] = n->x2[i]; ty2[i] = n->y2[i];
        td[i] = n->data[i]; tc[i] = n->child[i];
    }
    n->n = 0; nn->n = 0;
    n->x1[0] = tx1[seed1]; n->y1[0] = ty1[seed1];
    n->x2[0] = tx2[seed1]; n->y2[0] = ty2[seed1];
    n->data[0] = td[seed1]; n->child[0] = tc[seed1];
    n->n = 1;
    nn->x1[0] = tx1[seed2]; nn->y1[0] = ty1[seed2];
    nn->x2[0] = tx2[seed2]; nn->y2[0] = ty2[seed2];
    nn->data[0] = td[seed2]; nn->child[0] = tc[seed2];
    nn->n = 1;
    recompute_mbr(n); recompute_mbr(nn);
    for (size_t i = 0; i < total; i++) {
        if (i == seed1 || i == seed2) continue;
        double e1 = node_enlarge(n, tx1[i], ty1[i], tx2[i], ty2[i]);
        double e2 = node_enlarge(nn, tx1[i], ty1[i], tx2[i], ty2[i]);
        rt_node_t* tgt;
        if (e1 < e2) tgt = n;
        else if (e2 < e1) tgt = nn;
        else tgt = n->n <= nn->n ? n : nn;
        tgt->x1[tgt->n] = tx1[i]; tgt->y1[tgt->n] = ty1[i];
        tgt->x2[tgt->n] = tx2[i]; tgt->y2[tgt->n] = ty2[i];
        tgt->data[tgt->n] = td[i]; tgt->child[tgt->n] = tc[i];
        tgt->n++;
        mbr_expand(&tgt->bx1, &tgt->by1, &tgt->bx2, &tgt->by2,
                   tx1[i], ty1[i], tx2[i], ty2[i]);
    }
}

/* Insert (rect,data,child) entry into node. If overflow, split and return sibling via *sib. */
static int node_add(rt_node_t* node, double x1, double y1, double x2, double y2,
                     ds_val_t data, rt_node_t* child, rt_node_t** sib) {
    *sib = NULL;
    node->x1[node->n] = x1; node->y1[node->n] = y1;
    node->x2[node->n] = x2; node->y2[node->n] = y2;
    node->data[node->n] = data;
    node->child[node->n] = child;
    node->n++;
    mbr_expand(&node->bx1, &node->by1, &node->bx2, &node->by2, x1, y1, x2, y2);
    if (node->n > RT_MAX) {
        rt_node_t* nn = rt_node_new(node->leaf);
        if (!nn) return -1;
        linear_split(node, nn);
        *sib = nn;
    }
    return 0;
}

/* The below block uses R-TREE INSERT WITH SPLIT PROPAGATION: descend by
 * choosing the child whose MBR needs the LEAST enlargement to fit the new
 * rect, then re-tighten MBRs on the way back up. If a node overflows, a
 * sibling bubbles up and the parent absorbs it (possibly splitting too).
 * That bottom-up split is the R-tree's balancing mechanism. */
/* Recursively insert a leaf entry (x1,y1,x2,y2,data). Returns sibling via *sib. */
static int rt_insert(rt_node_t* node,
                      double x1, double y1, double x2, double y2, ds_val_t data,
                      rt_node_t** sib) {
    if (node->leaf) {
        return node_add(node, x1, y1, x2, y2, data, NULL, sib);
    }
    /* Choose subtree. */
    size_t best = 0;
    double best_e = DBL_MAX, best_a = DBL_MAX;
    for (size_t i = 0; i < node->n; i++) {
        double nx1 = node->x1[i] < x1 ? node->x1[i] : x1;
        double ny1 = node->y1[i] < y1 ? node->y1[i] : y1;
        double nx2 = node->x2[i] > x2 ? node->x2[i] : x2;
        double ny2 = node->y2[i] > y2 ? node->y2[i] : y2;
        double old = rt_area(node->x1[i], node->y1[i], node->x2[i], node->y2[i]);
        double e = rt_area(nx1, ny1, nx2, ny2) - old;
        if (e < best_e || (e == best_e && old < best_a)) {
            best_e = e; best_a = old; best = i;
        }
    }
    rt_node_t* sub_sib = NULL;
    if (rt_insert(node->child[best], x1, y1, x2, y2, data, &sub_sib) != 0) return -1;
    /* Update child's MBR in this entry. */
    node->x1[best] = node->child[best]->bx1;
    node->y1[best] = node->child[best]->by1;
    node->x2[best] = node->child[best]->bx2;
    node->y2[best] = node->child[best]->by2;
    recompute_mbr(node);
    if (sub_sib) {
        return node_add(node,
                         sub_sib->bx1, sub_sib->by1, sub_sib->bx2, sub_sib->by2,
                         NULL, sub_sib, sib);
    }
    *sib = NULL;
    return 0;
}

ds_status_t sp_rtree_insert(sp_rtree_t* t, double x1, double y1, double x2, double y2, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    if (x2 < x1) { double tmp = x1; x1 = x2; x2 = tmp; }
    if (y2 < y1) { double tmp = y1; y1 = y2; y2 = tmp; }
    rt_node_t* sib = NULL;
    if (rt_insert(t->root, x1, y1, x2, y2, v, &sib) != 0) return DS_ERR_NOMEM;
    if (sib) {
        rt_node_t* new_root = rt_node_new(0);
        if (!new_root) return DS_ERR_NOMEM;
        new_root->child[0] = t->root;
        new_root->x1[0] = t->root->bx1; new_root->y1[0] = t->root->by1;
        new_root->x2[0] = t->root->bx2; new_root->y2[0] = t->root->by2;
        new_root->data[0] = NULL;
        new_root->child[1] = sib;
        new_root->x1[1] = sib->bx1; new_root->y1[1] = sib->by1;
        new_root->x2[1] = sib->bx2; new_root->y2[1] = sib->by2;
        new_root->data[1] = NULL;
        new_root->n = 2;
        recompute_mbr(new_root);
        t->root = new_root;
    }
    return DS_OK;
}

static int rects_overlap(double ax1, double ay1, double ax2, double ay2,
                          double bx1, double by1, double bx2, double by2) {
    if (ax2 < bx1 || bx2 < ax1) return 0;
    if (ay2 < by1 || by2 < ay1) return 0;
    return 1;
}

/* The below block uses R-TREE SEARCH (descend any overlapping MBR): if the
 * node's bounding rectangle doesn't intersect the query, the entire subtree
 * is skipped; otherwise recurse into every child whose MBR overlaps. This is
 * what makes "which station coverage rectangles touch this viewport?" cheap. */
static void rt_search(const rt_node_t* n,
                       double x1, double y1, double x2, double y2,
                       sp_rect_t* out, size_t max, size_t* cnt) {
    if (!n || n->n == 0) return;
    if (!rects_overlap(n->bx1, n->by1, n->bx2, n->by2, x1, y1, x2, y2)) return;
    if (n->leaf) {
        for (size_t i = 0; i < n->n; i++) {
            if (rects_overlap(n->x1[i], n->y1[i], n->x2[i], n->y2[i], x1, y1, x2, y2)) {
                if (*cnt < max) {
                    out[*cnt].x1 = n->x1[i]; out[*cnt].y1 = n->y1[i];
                    out[*cnt].x2 = n->x2[i]; out[*cnt].y2 = n->y2[i];
                    out[*cnt].data = n->data[i];
                }
                (*cnt)++;
            }
        }
    } else {
        for (size_t i = 0; i < n->n; i++) {
            if (rects_overlap(n->x1[i], n->y1[i], n->x2[i], n->y2[i], x1, y1, x2, y2)) {
                rt_search(n->child[i], x1, y1, x2, y2, out, max, cnt);
            }
        }
    }
}

size_t sp_rtree_search(const sp_rtree_t* t, double x1, double y1, double x2, double y2,
                       sp_rect_t* out, size_t max) {
    if (!t) return 0;
    if (x2 < x1) { double tmp = x1; x1 = x2; x2 = tmp; }
    if (y2 < y1) { double tmp = y1; y1 = y2; y2 = tmp; }
    size_t cnt = 0;
    rt_search(t->root, x1, y1, x2, y2, out, max, &cnt);
    return cnt < max ? cnt : max;
}
