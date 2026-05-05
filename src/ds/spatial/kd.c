/* kd.c — 2D kd-tree. */
#include "dispatch/spatial.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* The below block uses the KD-TREE node layout: each node holds a 2D point
 * plus the `axis` it splits on (alternating x/y by depth). The splitting
 * axis is what lets the tree partition k-D space into nested half-planes,
 * which the simulation uses for fast 2D point queries. */
typedef struct kd_node {
    sp_point_t p;
    int axis; /* 0 = x, 1 = y */
    struct kd_node* l;
    struct kd_node* r;
} kd_node_t;

typedef struct sp_kd {
    kd_node_t* root;
    size_t n;
} sp_kd_t;

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

/* The below block uses the KD-TREE BUILD by MEDIAN SPLIT: sort the slab on
 * the current axis, take the median as the node, recurse on each half with
 * the axis toggled. Building from the median keeps the tree balanced so that
 * later nearest-neighbour queries run in expected O(log n). */
static kd_node_t* kd_build_rec(sp_point_t* pts, size_t n, int depth) {
    if (n == 0) return NULL;
    int axis = depth % 2;
    qsort(pts, n, sizeof(sp_point_t), axis == 0 ? cmp_x : cmp_y);
    size_t mid = n / 2;
    kd_node_t* node = (kd_node_t*)malloc(sizeof(kd_node_t));
    if (!node) return NULL;
    node->p = pts[mid];
    node->axis = axis;
    node->l = kd_build_rec(pts, mid, depth + 1);
    node->r = kd_build_rec(pts + mid + 1, n - mid - 1, depth + 1);
    return node;
}

sp_kd_t* sp_kd_build(const sp_point_t* pts, size_t n) {
    sp_kd_t* t = (sp_kd_t*)calloc(1, sizeof(sp_kd_t));
    if (!t) return NULL;
    if (n == 0) return t;
    sp_point_t* copy = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
    if (!copy) { free(t); return NULL; }
    memcpy(copy, pts, sizeof(sp_point_t) * n);
    t->root = kd_build_rec(copy, n, 0);
    t->n = n;
    free(copy);
    return t;
}

static void kd_free(kd_node_t* n) {
    if (!n) return;
    kd_free(n->l);
    kd_free(n->r);
    free(n);
}

void sp_kd_destroy(sp_kd_t* t) {
    if (!t) return;
    kd_free(t->root);
    free(t);
}

static kd_node_t* kd_insert_rec(kd_node_t* node, double x, double y, ds_val_t v, int depth) {
    if (!node) {
        kd_node_t* nn = (kd_node_t*)malloc(sizeof(kd_node_t));
        if (!nn) return NULL;
        nn->p.x = x; nn->p.y = y; nn->p.data = v;
        nn->axis = depth % 2;
        nn->l = nn->r = NULL;
        return nn;
    }
    double v1 = node->axis == 0 ? x : y;
    double v2 = node->axis == 0 ? node->p.x : node->p.y;
    if (v1 < v2) {
        kd_node_t* c = kd_insert_rec(node->l, x, y, v, depth + 1);
        if (!c) return NULL;
        node->l = c;
    } else {
        kd_node_t* c = kd_insert_rec(node->r, x, y, v, depth + 1);
        if (!c) return NULL;
        node->r = c;
    }
    return node;
}

ds_status_t sp_kd_insert(sp_kd_t* t, double x, double y, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    if (!t->root) {
        kd_node_t* nn = (kd_node_t*)malloc(sizeof(kd_node_t));
        if (!nn) return DS_ERR_NOMEM;
        nn->p.x = x; nn->p.y = y; nn->p.data = v;
        nn->axis = 0;
        nn->l = nn->r = NULL;
        t->root = nn;
    } else {
        kd_node_t* r = kd_insert_rec(t->root, x, y, v, 0);
        if (!r) return DS_ERR_NOMEM;
    }
    t->n++;
    return DS_OK;
}

/* Max-heap by distance for k-NN. */
typedef struct { sp_point_t p; double d2; } kd_item_t;

static void heap_up(kd_item_t* h, size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (h[parent].d2 < h[i].d2) {
            kd_item_t tmp = h[parent]; h[parent] = h[i]; h[i] = tmp;
            i = parent;
        } else break;
    }
}
static void heap_down(kd_item_t* h, size_t n, size_t i) {
    for (;;) {
        size_t l = 2*i+1, r = 2*i+2, big = i;
        if (l < n && h[l].d2 > h[big].d2) big = l;
        if (r < n && h[r].d2 > h[big].d2) big = r;
        if (big == i) break;
        kd_item_t tmp = h[big]; h[big] = h[i]; h[i] = tmp;
        i = big;
    }
}

/* The below block uses KD-TREE NEAREST-NEIGHBOUR with BOUNDING-BOX PRUNING.
 * It descends into the closer child first, maintains the k best points in a
 * bounded max-heap, and only visits the far child when its splitting plane
 * could still contain something closer than the current worst. The pruning
 * is what turns brute-force O(n) k-NN into expected O(log n) per query. */
static void kd_nn_rec(const kd_node_t* node, double qx, double qy,
                       kd_item_t* heap, size_t* hn, size_t k) {
    if (!node) return;
    double dx = node->p.x - qx, dy = node->p.y - qy;
    double d2 = dx*dx + dy*dy;
    if (*hn < k) {
        heap[*hn].p = node->p;
        heap[*hn].d2 = d2;
        (*hn)++;
        heap_up(heap, *hn - 1);
    } else if (d2 < heap[0].d2) {
        heap[0].p = node->p;
        heap[0].d2 = d2;
        heap_down(heap, *hn, 0);
    }
    double qv = node->axis == 0 ? qx : qy;
    double pv = node->axis == 0 ? node->p.x : node->p.y;
    double diff = qv - pv;
    const kd_node_t* first = diff < 0 ? node->l : node->r;
    const kd_node_t* second = diff < 0 ? node->r : node->l;
    kd_nn_rec(first, qx, qy, heap, hn, k);
    if (*hn < k || diff*diff < heap[0].d2) {
        kd_nn_rec(second, qx, qy, heap, hn, k);
    }
}

static int kd_item_cmp(const void* a, const void* b) {
    double da = ((const kd_item_t*)a)->d2;
    double db = ((const kd_item_t*)b)->d2;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

size_t sp_kd_nearest(const sp_kd_t* t, double x, double y, size_t k, sp_point_t* out) {
    if (!t || !t->root || k == 0) return 0;
    kd_item_t* heap = (kd_item_t*)malloc(sizeof(kd_item_t) * k);
    if (!heap) return 0;
    size_t hn = 0;
    kd_nn_rec(t->root, x, y, heap, &hn, k);
    qsort(heap, hn, sizeof(kd_item_t), kd_item_cmp);
    for (size_t i = 0; i < hn; i++) out[i] = heap[i].p;
    free(heap);
    return hn;
}
