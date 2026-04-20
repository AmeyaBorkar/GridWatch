/* Double-ended PQ implemented as an interval (min-max) heap on a dynamic array.
 * Level 0,2,4,... are min-levels; 1,3,5,... are max-levels. */
#include "dispatch/heaps.h"
#include <stdlib.h>
#include <string.h>

struct depq {
    ds_entry_t* a;
    size_t n, cap;
};

depq_t* depq_create(void) {
    depq_t* d = (depq_t*)calloc(1, sizeof(*d));
    return d;
}

void depq_destroy(depq_t* d) {
    if (!d) return;
    free(d->a);
    free(d);
}

size_t depq_size(const depq_t* d) { return d ? d->n : 0; }

static int is_min_level(size_t i) {
    /* level = floor(log2(i+1)); min if even */
    size_t x = i + 1;
    int level = 0;
    while (x > 1) { x >>= 1; level++; }
    return (level & 1) == 0;
}

static void swap_entry(ds_entry_t* a, ds_entry_t* b) {
    ds_entry_t t = *a; *a = *b; *b = t;
}

static size_t parent_idx(size_t i) { return (i - 1) / 2; }
static size_t grand_idx(size_t i)  { return (i - 3) / 4; }

static size_t min_desc(ds_entry_t* a, size_t n, size_t i, int want_min) {
    /* Return index of min (or max) among i's children and grandchildren. */
    size_t best = i;
    size_t children[6];
    size_t cnt = 0;
    size_t c1 = 2*i + 1, c2 = 2*i + 2;
    if (c1 < n) children[cnt++] = c1;
    if (c2 < n) children[cnt++] = c2;
    for (size_t k = 0; k < 2 && (2*i + 1 + k) < n; k++) {
        size_t ch = 2*i + 1 + k;
        size_t g1 = 2*ch + 1, g2 = 2*ch + 2;
        if (g1 < n) children[cnt++] = g1;
        if (g2 < n) children[cnt++] = g2;
    }
    for (size_t k = 0; k < cnt; k++) {
        if (want_min) {
            if (a[children[k]].key < a[best].key) best = children[k];
        } else {
            if (a[children[k]].key > a[best].key) best = children[k];
        }
    }
    return best == i ? (size_t)-1 : best;
}

static void trickle_down(ds_entry_t* a, size_t n, size_t i) {
    int want_min = is_min_level(i);
    while (1) {
        size_t m = min_desc(a, n, i, want_min);
        if (m == (size_t)-1) break;
        int is_grand = (m >= 2*i + 3); /* grandchildren indices start at 4i+3 */
        /* actually test via parent-of-m */
        is_grand = (m > 2*i + 2);
        if (is_grand) {
            if ((want_min && a[m].key < a[i].key) ||
                (!want_min && a[m].key > a[i].key)) {
                swap_entry(&a[m], &a[i]);
                size_t pm = parent_idx(m);
                if ((want_min && a[m].key > a[pm].key) ||
                    (!want_min && a[m].key < a[pm].key)) {
                    swap_entry(&a[m], &a[pm]);
                }
                i = m;
            } else break;
        } else {
            if ((want_min && a[m].key < a[i].key) ||
                (!want_min && a[m].key > a[i].key)) {
                swap_entry(&a[m], &a[i]);
            }
            break;
        }
    }
}

static void bubble_up_min(ds_entry_t* a, size_t i) {
    while (i >= 3) {
        size_t g = grand_idx(i);
        if (a[i].key < a[g].key) {
            swap_entry(&a[i], &a[g]);
            i = g;
        } else break;
    }
}

static void bubble_up_max(ds_entry_t* a, size_t i) {
    while (i >= 3) {
        size_t g = grand_idx(i);
        if (a[i].key > a[g].key) {
            swap_entry(&a[i], &a[g]);
            i = g;
        } else break;
    }
}

static void bubble_up(ds_entry_t* a, size_t i) {
    if (i == 0) return;
    size_t p = parent_idx(i);
    if (is_min_level(i)) {
        if (a[i].key > a[p].key) {
            swap_entry(&a[i], &a[p]);
            bubble_up_max(a, p);
        } else {
            bubble_up_min(a, i);
        }
    } else {
        if (a[i].key < a[p].key) {
            swap_entry(&a[i], &a[p]);
            bubble_up_min(a, p);
        } else {
            bubble_up_max(a, i);
        }
    }
}

ds_status_t depq_push(depq_t* d, ds_key_t key, ds_val_t val) {
    if (!d) return DS_ERR_INVALID;
    if (d->n == d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 16;
        ds_entry_t* tmp = (ds_entry_t*)realloc(d->a, nc * sizeof(*tmp));
        if (!tmp) return DS_ERR_NOMEM;
        d->a = tmp;
        d->cap = nc;
    }
    d->a[d->n].key = key;
    d->a[d->n].val = val;
    bubble_up(d->a, d->n);
    d->n++;
    return DS_OK;
}

ds_status_t depq_peek_min(const depq_t* d, ds_entry_t* out) {
    if (!d || !out) return DS_ERR_INVALID;
    if (d->n == 0) return DS_ERR_EMPTY;
    *out = d->a[0];
    return DS_OK;
}

ds_status_t depq_peek_max(const depq_t* d, ds_entry_t* out) {
    if (!d || !out) return DS_ERR_INVALID;
    if (d->n == 0) return DS_ERR_EMPTY;
    if (d->n == 1) { *out = d->a[0]; return DS_OK; }
    if (d->n == 2) { *out = d->a[1]; return DS_OK; }
    *out = d->a[d->a[1].key >= d->a[2].key ? 1 : 2];
    return DS_OK;
}

ds_status_t depq_pop_min(depq_t* d, ds_entry_t* out) {
    if (!d) return DS_ERR_INVALID;
    if (d->n == 0) return DS_ERR_EMPTY;
    if (out) *out = d->a[0];
    d->n--;
    if (d->n > 0) {
        d->a[0] = d->a[d->n];
        trickle_down(d->a, d->n, 0);
    }
    return DS_OK;
}

ds_status_t depq_pop_max(depq_t* d, ds_entry_t* out) {
    if (!d) return DS_ERR_INVALID;
    if (d->n == 0) return DS_ERR_EMPTY;
    if (d->n == 1) {
        if (out) *out = d->a[0];
        d->n = 0;
        return DS_OK;
    }
    size_t idx = 1;
    if (d->n >= 3 && d->a[2].key > d->a[1].key) idx = 2;
    if (out) *out = d->a[idx];
    d->n--;
    if (idx != d->n) {
        d->a[idx] = d->a[d->n];
        trickle_down(d->a, d->n, idx);
    }
    return DS_OK;
}
