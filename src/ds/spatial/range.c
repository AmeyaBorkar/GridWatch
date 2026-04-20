/* range.c — simple 2D range tree: primary tree on x, each node stores sorted-by-y point list. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>

typedef struct rn {
    sp_point_t p;      /* median point (by x) */
    struct rn* l;
    struct rn* r;
    sp_point_t* y_sorted;  /* all points in this subtree, sorted by y */
    size_t ny;
    double x_min, x_max;
} rn_t;

typedef struct sp_range {
    rn_t* root;
} sp_range_t;

static int cmp_px(const void* a, const void* b) {
    double ax = ((const sp_point_t*)a)->x;
    double bx = ((const sp_point_t*)b)->x;
    if (ax < bx) return -1;
    if (ax > bx) return 1;
    return 0;
}
static int cmp_py(const void* a, const void* b) {
    double ay = ((const sp_point_t*)a)->y;
    double by = ((const sp_point_t*)b)->y;
    if (ay < by) return -1;
    if (ay > by) return 1;
    return 0;
}

/* pts must be sorted by x. */
static rn_t* build_rec(sp_point_t* pts, size_t n) {
    if (n == 0) return NULL;
    rn_t* node = (rn_t*)calloc(1, sizeof(rn_t));
    if (!node) return NULL;
    size_t mid = n / 2;
    node->p = pts[mid];
    node->x_min = pts[0].x;
    node->x_max = pts[n-1].x;
    node->y_sorted = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
    if (!node->y_sorted) { free(node); return NULL; }
    memcpy(node->y_sorted, pts, sizeof(sp_point_t) * n);
    qsort(node->y_sorted, n, sizeof(sp_point_t), cmp_py);
    node->ny = n;
    node->l = build_rec(pts, mid);
    node->r = build_rec(pts + mid + 1, n - mid - 1);
    return node;
}

sp_range_t* sp_range_build(const sp_point_t* pts, size_t n) {
    sp_range_t* t = (sp_range_t*)calloc(1, sizeof(sp_range_t));
    if (!t) return NULL;
    if (n == 0) return t;
    sp_point_t* copy = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
    if (!copy) { free(t); return NULL; }
    memcpy(copy, pts, sizeof(sp_point_t) * n);
    qsort(copy, n, sizeof(sp_point_t), cmp_px);
    t->root = build_rec(copy, n);
    free(copy);
    return t;
}

static void rn_free(rn_t* n) {
    if (!n) return;
    rn_free(n->l);
    rn_free(n->r);
    free(n->y_sorted);
    free(n);
}

void sp_range_destroy(sp_range_t* t) {
    if (!t) return;
    rn_free(t->root);
    free(t);
}

static void rn_report(const rn_t* n, double y1, double y2,
                       sp_point_t* out, size_t max, size_t* cnt) {
    /* Emit points from y_sorted within [y1, y2]. */
    for (size_t i = 0; i < n->ny; i++) {
        if (n->y_sorted[i].y < y1) continue;
        if (n->y_sorted[i].y > y2) break;
        if (*cnt < max) out[*cnt] = n->y_sorted[i];
        (*cnt)++;
    }
}

static void rn_search(const rn_t* n, double x1, double y1, double x2, double y2,
                       sp_point_t* out, size_t max, size_t* cnt) {
    if (!n) return;
    if (n->x_max < x1 || n->x_min > x2) return;
    if (n->x_min >= x1 && n->x_max <= x2) {
        rn_report(n, y1, y2, out, max, cnt);
        return;
    }
    if (n->p.x >= x1 && n->p.x <= x2 && n->p.y >= y1 && n->p.y <= y2) {
        if (*cnt < max) out[*cnt] = n->p;
        (*cnt)++;
    }
    rn_search(n->l, x1, y1, x2, y2, out, max, cnt);
    rn_search(n->r, x1, y1, x2, y2, out, max, cnt);
}

size_t sp_range_search(const sp_range_t* t, double x1, double y1, double x2, double y2,
                       sp_point_t* out, size_t max) {
    if (!t || !t->root) return 0;
    if (x2 < x1) { double tmp = x1; x1 = x2; x2 = tmp; }
    if (y2 < y1) { double tmp = y1; y1 = y2; y2 = tmp; }
    size_t cnt = 0;
    rn_search(t->root, x1, y1, x2, y2, out, max, &cnt);
    return cnt < max ? cnt : max;
}
