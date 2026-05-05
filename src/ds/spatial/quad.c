/* quad.c — point quadtree. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QUAD_CAP 4

/* Quadtree node: holds an axis-aligned bounding box, a small bucket of points
 * (up to QUAD_CAP=4), and four optional children for the NW/NE/SW/SE quadrants.
 * Lazy growth: a node only subdivides when the bucket overflows. Used in the
 * sim to find the nearest idle unit to a new incident. */
typedef struct sp_quad {
    double x, y, w, h; /* bounds (x,y) = top-left corner, w,h = size */
    sp_point_t pts[QUAD_CAP];
    size_t n;
    struct sp_quad* nw;
    struct sp_quad* ne;
    struct sp_quad* sw;
    struct sp_quad* se;
    int divided;
} sp_quad_t;

static sp_quad_t* quad_new(double x, double y, double w, double h) {
    sp_quad_t* q = (sp_quad_t*)calloc(1, sizeof(sp_quad_t));
    if (!q) return NULL;
    q->x = x; q->y = y; q->w = w; q->h = h;
    return q;
}

sp_quad_t* sp_quad_create(double x, double y, double w, double h) {
    return quad_new(x, y, w, h);
}

void sp_quad_destroy(sp_quad_t* q) {
    if (!q) return;
    if (q->divided) {
        sp_quad_destroy(q->nw);
        sp_quad_destroy(q->ne);
        sp_quad_destroy(q->sw);
        sp_quad_destroy(q->se);
    }
    free(q);
}

static int quad_contains(const sp_quad_t* q, double x, double y) {
    return x >= q->x && x <= q->x + q->w && y >= q->y && y <= q->y + q->h;
}

static int rect_intersects(double ax, double ay, double aw, double ah,
                            double bx, double by, double bw, double bh) {
    if (ax > bx + bw || bx > ax + aw) return 0;
    if (ay > by + bh || by > ay + ah) return 0;
    return 1;
}

/* The below block performs the 4-way spatial subdivision: split this node's
 * bounding box into four equal child quadrants (NW/NE/SW/SE). Called when
 * the bucket overflows — this is what makes the tree grow lazily. */
static int quad_subdivide(sp_quad_t* q) {
    double hw = q->w / 2.0, hh = q->h / 2.0;
    q->nw = quad_new(q->x, q->y, hw, hh);
    q->ne = quad_new(q->x + hw, q->y, hw, hh);
    q->sw = quad_new(q->x, q->y + hh, hw, hh);
    q->se = quad_new(q->x + hw, q->y + hh, hw, hh);
    if (!q->nw || !q->ne || !q->sw || !q->se) {
        free(q->nw); free(q->ne); free(q->sw); free(q->se);
        q->nw = q->ne = q->sw = q->se = NULL;
        return 0;
    }
    q->divided = 1;
    return 1;
}

/* The below block does a recursive insert with lazy split: drop the point
 * into this node if there's room (bucket < cap); otherwise subdivide on
 * demand and recurse into the matching child. Avoids paying for subdivision
 * until it's actually needed. */
static ds_status_t quad_insert_rec(sp_quad_t* q, double x, double y, ds_val_t v) {
    if (!quad_contains(q, x, y)) return DS_ERR_INVALID;
    if (!q->divided && q->n < QUAD_CAP) {
        q->pts[q->n].x = x;
        q->pts[q->n].y = y;
        q->pts[q->n].data = v;
        q->n++;
        return DS_OK;
    }
    if (!q->divided) {
        if (!quad_subdivide(q)) return DS_ERR_NOMEM;
    }
    if (quad_insert_rec(q->nw, x, y, v) == DS_OK) return DS_OK;
    if (quad_insert_rec(q->ne, x, y, v) == DS_OK) return DS_OK;
    if (quad_insert_rec(q->sw, x, y, v) == DS_OK) return DS_OK;
    if (quad_insert_rec(q->se, x, y, v) == DS_OK) return DS_OK;
    return DS_ERR_INVALID;
}

ds_status_t sp_quad_insert(sp_quad_t* q, double x, double y, ds_val_t v) {
    if (!q) return DS_ERR_INVALID;
    return quad_insert_rec(q, x, y, v);
}

/* The below block is the range query — and the rect_intersects check on the
 * very first line is the entire speedup of a quadtree: if this node's bbox
 * doesn't overlap the query rect, we prune the whole subtree (no recursion).
 * Without that prune you'd just be doing a linear scan with extra steps. */
static void quad_query_rec(const sp_quad_t* q, double rx, double ry, double rw, double rh,
                            sp_point_t* out, size_t max, size_t* cnt) {
    if (!rect_intersects(q->x, q->y, q->w, q->h, rx, ry, rw, rh)) return;
    for (size_t i = 0; i < q->n; i++) {
        double px = q->pts[i].x, py = q->pts[i].y;
        if (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh) {
            if (*cnt < max) out[*cnt] = q->pts[i];
            (*cnt)++;
        }
    }
    if (q->divided) {
        quad_query_rec(q->nw, rx, ry, rw, rh, out, max, cnt);
        quad_query_rec(q->ne, rx, ry, rw, rh, out, max, cnt);
        quad_query_rec(q->sw, rx, ry, rw, rh, out, max, cnt);
        quad_query_rec(q->se, rx, ry, rw, rh, out, max, cnt);
    }
}

size_t sp_quad_query(const sp_quad_t* q, double x, double y, double w, double h,
                     sp_point_t* out, size_t max) {
    if (!q) return 0;
    size_t cnt = 0;
    quad_query_rec(q, x, y, w, h, out, max, &cnt);
    return cnt < max ? cnt : max;
}

/* Collect all points, sort by distance, return k. */
typedef struct { sp_point_t p; double d2; } nn_item_t;

static void quad_collect(const sp_quad_t* q, nn_item_t* arr, size_t* n, size_t cap,
                          double cx, double cy) {
    if (*n >= cap) return;
    for (size_t i = 0; i < q->n && *n < cap; i++) {
        double dx = q->pts[i].x - cx, dy = q->pts[i].y - cy;
        arr[*n].p = q->pts[i];
        arr[*n].d2 = dx*dx + dy*dy;
        (*n)++;
    }
    if (q->divided) {
        quad_collect(q->nw, arr, n, cap, cx, cy);
        quad_collect(q->ne, arr, n, cap, cx, cy);
        quad_collect(q->sw, arr, n, cap, cx, cy);
        quad_collect(q->se, arr, n, cap, cx, cy);
    }
}

static size_t quad_count(const sp_quad_t* q) {
    size_t c = q->n;
    if (q->divided) {
        c += quad_count(q->nw);
        c += quad_count(q->ne);
        c += quad_count(q->sw);
        c += quad_count(q->se);
    }
    return c;
}

static int nn_cmp(const void* a, const void* b) {
    double da = ((const nn_item_t*)a)->d2;
    double db = ((const nn_item_t*)b)->d2;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* The below block does k-nearest-neighbor the simple way: collect every point
 * in the tree, compute squared distance, sort, return the k smallest. Not the
 * theoretically optimal nearest algorithm, but it works well at the small
 * scales the dispatcher uses (handful of idle units). */
size_t sp_quad_nearest(const sp_quad_t* q, double x, double y, size_t k, sp_point_t* out) {
    if (!q || k == 0) return 0;
    size_t total = quad_count(q);
    if (total == 0) return 0;
    nn_item_t* arr = (nn_item_t*)malloc(sizeof(nn_item_t) * total);
    if (!arr) return 0;
    size_t n = 0;
    quad_collect(q, arr, &n, total, x, y);
    qsort(arr, n, sizeof(nn_item_t), nn_cmp);
    size_t r = n < k ? n : k;
    for (size_t i = 0; i < r; i++) out[i] = arr[i].p;
    free(arr);
    return r;
}
