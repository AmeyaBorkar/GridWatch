/* bsp.c — axis-aligned BSP over points. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>

#define BSP_LEAF_CAP 4

/* The below block uses the BSP TREE node layout: an internal node stores a
 * splitting axis + scalar (an axis-aligned hyperplane) carving space into
 * two half-spaces; leaves bucket up to BSP_LEAF_CAP points. The static
 * partition is what enables fast point-locate over fixed station locations. */
typedef struct bn {
    int leaf;
    /* Leaf data: */
    sp_point_t* pts;
    size_t n;
    /* Internal: */
    int axis; /* 0 = x, 1 = y */
    double split;
    struct bn* l;
    struct bn* r;
} bn_t;

typedef struct sp_bsp {
    bn_t* root;
} sp_bsp_t;

static int cmp_x(const void* a, const void* b) {
    double ax = ((const sp_point_t*)a)->x;
    double bx = ((const sp_point_t*)b)->x;
    if (ax < bx) return -1;
    if (ax > bx) return 1;
    return 0;
}
static int cmp_y(const void* a, const void* b) {
    double ay = ((const sp_point_t*)a)->y;
    double by = ((const sp_point_t*)b)->y;
    if (ay < by) return -1;
    if (ay > by) return 1;
    return 0;
}

/* The below block uses BSP TREE BUILD: pick a splitting line (axis alternates
 * with depth, split value = median on that axis), then recurse on each side
 * until the leaf cap is reached. Median-splitting keeps the tree balanced so
 * that point-locate is O(log n) over the static station layout. */
static bn_t* bsp_build_rec(sp_point_t* pts, size_t n, int depth) {
    bn_t* node = (bn_t*)calloc(1, sizeof(bn_t));
    if (!node) return NULL;
    if (n <= BSP_LEAF_CAP) {
        node->leaf = 1;
        node->n = n;
        if (n > 0) {
            node->pts = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
            if (!node->pts) { free(node); return NULL; }
            memcpy(node->pts, pts, sizeof(sp_point_t) * n);
        }
        return node;
    }
    /* Pick axis alternating by depth; classify by median. */
    int axis = depth % 2;
    qsort(pts, n, sizeof(sp_point_t), axis == 0 ? cmp_x : cmp_y);
    size_t mid = n / 2;
    double split = axis == 0 ? pts[mid].x : pts[mid].y;
    /* Split: left = pts[0..mid-1], right = pts[mid..n-1]. Median goes to right. */
    node->leaf = 0;
    node->axis = axis;
    node->split = split;
    node->l = bsp_build_rec(pts, mid, depth + 1);
    node->r = bsp_build_rec(pts + mid, n - mid, depth + 1);
    if (!node->l || !node->r) { /* leave partial tree for destroy. */ }
    return node;
}

sp_bsp_t* sp_bsp_build(const sp_point_t* pts, size_t n) {
    sp_bsp_t* t = (sp_bsp_t*)calloc(1, sizeof(sp_bsp_t));
    if (!t) return NULL;
    if (n == 0) return t;
    sp_point_t* copy = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
    if (!copy) { free(t); return NULL; }
    memcpy(copy, pts, sizeof(sp_point_t) * n);
    t->root = bsp_build_rec(copy, n, 0);
    free(copy);
    return t;
}

static void bsp_free(bn_t* n) {
    if (!n) return;
    if (n->leaf) {
        free(n->pts);
    } else {
        bsp_free(n->l);
        bsp_free(n->r);
    }
    free(n);
}

void sp_bsp_destroy(sp_bsp_t* t) {
    if (!t) return;
    bsp_free(t->root);
    free(t);
}

static int bsp_depth_rec(const bn_t* n) {
    if (!n) return 0;
    if (n->leaf) return 1;
    int dl = bsp_depth_rec(n->l);
    int dr = bsp_depth_rec(n->r);
    int m = dl > dr ? dl : dr;
    return m + 1;
}

int sp_bsp_depth(const sp_bsp_t* t) {
    if (!t || !t->root) return 0;
    return bsp_depth_rec(t->root);
}

/* The below block uses BSP POINT-LOCATE: walk the tree comparing the query
 * coordinate against each splitting plane (left if v < split else right)
 * until a leaf is reached. The leaf's bucket is the BSP region containing
 * that point — useful for "which partition does this map click belong to?". */
/* Return points in the leaf region containing (x,y). */
size_t sp_bsp_region(const sp_bsp_t* t, double x, double y, sp_point_t* out, size_t max) {
    if (!t || !t->root) return 0;
    const bn_t* cur = t->root;
    while (!cur->leaf) {
        double v = cur->axis == 0 ? x : y;
        if (v < cur->split) cur = cur->l;
        else cur = cur->r;
        if (!cur) return 0;
    }
    size_t cnt = cur->n < max ? cur->n : max;
    for (size_t i = 0; i < cnt; i++) out[i] = cur->pts[i];
    return cnt;
}
