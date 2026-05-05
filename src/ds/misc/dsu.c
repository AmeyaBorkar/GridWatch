/* dsu.c — union-by-rank disjoint set union with path compression. */
#include "dispatch/misc.h"
#include <stdlib.h>

/* DISJOINT SET UNION (UNION-FIND): parent[] is the forest representation —
 * parent[i] is i's parent, with roots satisfying parent[i] == i. rank[] is the
 * upper bound on tree height for union-by-rank; sz[] tracks component size for
 * O(1) component-size queries. Used to track which intersections remain
 * connected after some roads are blocked. */
struct misc_dsu {
    size_t* parent;
    size_t* rank;
    size_t* sz;
    size_t  n;
    size_t  components;
};

DS_API misc_dsu_t* misc_dsu_create(size_t n) {
    misc_dsu_t* d = (misc_dsu_t*)malloc(sizeof(*d));
    if (!d) return NULL;
    d->parent = (size_t*)malloc(sizeof(size_t) * (n ? n : 1));
    d->rank   = (size_t*)calloc(n ? n : 1, sizeof(size_t));
    d->sz     = (size_t*)malloc(sizeof(size_t) * (n ? n : 1));
    if (!d->parent || !d->rank || !d->sz) {
        free(d->parent); free(d->rank); free(d->sz); free(d);
        return NULL;
    }
    for (size_t i = 0; i < n; i++) { d->parent[i] = i; d->sz[i] = 1; }
    d->n = n;
    d->components = n;
    return d;
}

DS_API void misc_dsu_destroy(misc_dsu_t* d) {
    if (!d) return;
    free(d->parent); free(d->rank); free(d->sz);
    free(d);
}

/* FIND with PATH COMPRESSION: walk up to the root, then a second pass relinks
 * every node on the path directly to the root. Combined with union-by-rank,
 * this gives the famous near-constant inverse-Ackermann amortised cost. */
DS_API size_t misc_dsu_find(misc_dsu_t* d, size_t x) {
    if (!d || x >= d->n) return (size_t)-1;
    size_t r = x;
    while (d->parent[r] != r) r = d->parent[r];
    /* path compression */
    size_t cur = x;
    while (d->parent[cur] != r) {
        size_t nxt = d->parent[cur];
        d->parent[cur] = r;
        cur = nxt;
    }
    return r;
}

/* UNION BY RANK: attach the shorter tree under the taller one to keep depth
 * O(log n). Tie-breaks by incrementing rank. Together with path compression,
 * union and find run in amortised O(alpha(n)) — effectively O(1). */
DS_API int misc_dsu_union(misc_dsu_t* d, size_t a, size_t b) {
    if (!d || a >= d->n || b >= d->n) return 0;
    size_t ra = misc_dsu_find(d, a);
    size_t rb = misc_dsu_find(d, b);
    if (ra == rb) return 0;
    if (d->rank[ra] < d->rank[rb]) {
        size_t t = ra; ra = rb; rb = t;
    }
    d->parent[rb] = ra;
    d->sz[ra] += d->sz[rb];
    if (d->rank[ra] == d->rank[rb]) d->rank[ra]++;
    d->components--;
    return 1;
}

DS_API size_t misc_dsu_count(const misc_dsu_t* d) {
    return d ? d->components : 0;
}

DS_API size_t misc_dsu_size(const misc_dsu_t* d, size_t x) {
    if (!d || x >= d->n) return 0;
    /* non-const find via cast: traversal is harmless */
    misc_dsu_t* m = (misc_dsu_t*)d;
    size_t r = misc_dsu_find(m, x);
    return d->sz[r];
}
