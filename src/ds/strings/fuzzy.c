/* fuzzy.c — BK-tree over Levenshtein distance */
#include "dispatch/strings.h"
#include <stdlib.h>
#include <string.h>

typedef struct bk_edge {
    int dist;
    struct bk_node* child;
} bk_edge_t;

typedef struct bk_node {
    char* word;
    bk_edge_t* edges;
    size_t nedges;
} bk_node_t;

struct str_fuzzy {
    bk_node_t* root;
};

static int imin(int a, int b) { return a < b ? a : b; }

static int levenshtein(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0) return (int)lb;
    if (lb == 0) return (int)la;
    int* prev = malloc((lb + 1) * sizeof(int));
    int* cur = malloc((lb + 1) * sizeof(int));
    if (!prev || !cur) { free(prev); free(cur); return -1; }
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            cur[j] = imin(imin(cur[j-1] + 1, prev[j] + 1), prev[j-1] + cost);
        }
        int* t = prev; prev = cur; cur = t;
    }
    int r = prev[lb];
    free(prev); free(cur);
    return r;
}

static bk_node_t* bk_new(const char* w) {
    bk_node_t* n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    size_t l = strlen(w);
    n->word = malloc(l + 1);
    if (!n->word) { free(n); return NULL; }
    memcpy(n->word, w, l + 1);
    return n;
}

static void bk_free(bk_node_t* n) {
    if (!n) return;
    for (size_t i = 0; i < n->nedges; i++) bk_free(n->edges[i].child);
    free(n->edges);
    free(n->word);
    free(n);
}

str_fuzzy_t* str_fuzzy_create(void) {
    str_fuzzy_t* f = calloc(1, sizeof(*f));
    return f;
}

void str_fuzzy_destroy(str_fuzzy_t* f) {
    if (!f) return;
    bk_free(f->root);
    free(f);
}

ds_status_t str_fuzzy_add(str_fuzzy_t* f, const char* word) {
    if (!f || !word) return DS_ERR_INVALID;
    if (!f->root) {
        f->root = bk_new(word);
        return f->root ? DS_OK : DS_ERR_NOMEM;
    }
    bk_node_t* cur = f->root;
    for (;;) {
        int d = levenshtein(cur->word, word);
        if (d < 0) return DS_ERR_NOMEM;
        if (d == 0) return DS_ERR_DUP;
        bk_node_t* nxt = NULL;
        for (size_t i = 0; i < cur->nedges; i++) {
            if (cur->edges[i].dist == d) { nxt = cur->edges[i].child; break; }
        }
        if (!nxt) {
            bk_edge_t* ne = realloc(cur->edges, (cur->nedges + 1) * sizeof(*ne));
            if (!ne) return DS_ERR_NOMEM;
            cur->edges = ne;
            bk_node_t* child = bk_new(word);
            if (!child) return DS_ERR_NOMEM;
            cur->edges[cur->nedges].dist = d;
            cur->edges[cur->nedges].child = child;
            cur->nedges++;
            return DS_OK;
        }
        cur = nxt;
    }
}

static void bk_search(const bk_node_t* n, const char* q, int max_edits,
                      char** out, size_t max, size_t* count) {
    if (!n || *count >= max) return;
    int d = levenshtein(n->word, q);
    if (d < 0) return;
    if (d <= max_edits) {
        size_t l = strlen(n->word);
        char* s = malloc(l + 1);
        if (s) {
            memcpy(s, n->word, l + 1);
            out[(*count)++] = s;
        }
        if (*count >= max) return;
    }
    int lo = d - max_edits;
    int hi = d + max_edits;
    for (size_t i = 0; i < n->nedges; i++) {
        int ed = n->edges[i].dist;
        if (ed >= lo && ed <= hi) {
            bk_search(n->edges[i].child, q, max_edits, out, max, count);
            if (*count >= max) return;
        }
    }
}

size_t str_fuzzy_search(const str_fuzzy_t* f, const char* q, int max_edits,
                        char** out, size_t max) {
    if (!f || !q || !out || max == 0 || max_edits < 0) return 0;
    size_t count = 0;
    bk_search(f->root, q, max_edits, out, max, &count);
    return count;
}
