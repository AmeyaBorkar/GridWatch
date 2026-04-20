/* dawg.c — minimal DAWG built from sorted words (Daciuk et al.) */
#include "dispatch/strings.h"
#include <stdlib.h>
#include <string.h>

typedef struct dawg_state {
    int terminal;
    int n_trans;
    char* labels;                 /* sorted ascending */
    struct dawg_state** targets;
    /* hash chain for register */
    struct dawg_state* next;
    int finalized;                /* 1 if in register */
    size_t id;
} dawg_state_t;

#define REG_BUCKETS 4099

struct str_dawg {
    dawg_state_t* root;
    dawg_state_t** all;
    size_t n_all, cap_all;
    dawg_state_t* reg[REG_BUCKETS];
    char* prev_word;
    size_t finished;
    size_t state_count;            /* distinct finalized states + root */
};

static dawg_state_t* ds_new(str_dawg_t* d) {
    dawg_state_t* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (d->n_all == d->cap_all) {
        size_t nc = d->cap_all ? d->cap_all * 2 : 16;
        dawg_state_t** na = realloc(d->all, nc * sizeof(*na));
        if (!na) { free(s); return NULL; }
        d->all = na;
        d->cap_all = nc;
    }
    d->all[d->n_all++] = s;
    return s;
}

static int ds_add_trans(dawg_state_t* s, char c, dawg_state_t* tgt) {
    char* nl = realloc(s->labels, s->n_trans + 1);
    if (!nl) return -1;
    dawg_state_t** nt = realloc(s->targets, (s->n_trans + 1) * sizeof(*nt));
    if (!nt) { s->labels = nl; return -1; }
    s->labels = nl;
    s->targets = nt;
    s->labels[s->n_trans] = c;
    s->targets[s->n_trans] = tgt;
    s->n_trans++;
    return 0;
}

static dawg_state_t* ds_last_child(dawg_state_t* s) {
    if (s->n_trans == 0) return NULL;
    return s->targets[s->n_trans - 1];
}

static void ds_set_last_child(dawg_state_t* s, dawg_state_t* c) {
    s->targets[s->n_trans - 1] = c;
}

static unsigned long state_hash(const dawg_state_t* s) {
    unsigned long h = (unsigned long)(s->terminal ? 1u : 0u);
    h = h * 31u + (unsigned long)s->n_trans;
    for (int i = 0; i < s->n_trans; i++) {
        h = h * 131u + (unsigned char)s->labels[i];
        /* use pointer identity of target (targets already finalized) */
        h ^= (unsigned long)(uintptr_t)s->targets[i] + 0x9e3779b9u + (h << 6) + (h >> 2);
    }
    return h;
}

static int state_equiv(const dawg_state_t* a, const dawg_state_t* b) {
    if (a->terminal != b->terminal) return 0;
    if (a->n_trans != b->n_trans) return 0;
    for (int i = 0; i < a->n_trans; i++) {
        if (a->labels[i] != b->labels[i]) return 0;
        if (a->targets[i] != b->targets[i]) return 0;
    }
    return 1;
}

static dawg_state_t* reg_find_or_add(str_dawg_t* d, dawg_state_t* s) {
    unsigned long h = state_hash(s) % REG_BUCKETS;
    for (dawg_state_t* p = d->reg[h]; p; p = p->next) {
        if (state_equiv(p, s)) return p;
    }
    s->next = d->reg[h];
    d->reg[h] = s;
    s->finalized = 1;
    d->state_count++;
    return s;
}

/* Replace-or-register on last child of `s`. */
static void replace_or_register(str_dawg_t* d, dawg_state_t* s) {
    dawg_state_t* child = ds_last_child(s);
    if (!child) return;
    if (child->n_trans > 0) {
        replace_or_register(d, child);
    }
    dawg_state_t* rep = reg_find_or_add(d, child);
    if (rep != child) {
        ds_set_last_child(s, rep);
        /* orphan child; stays in d->all for destroy */
    }
}

static size_t common_prefix(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return i;
}

str_dawg_t* str_dawg_create(void) {
    str_dawg_t* d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->root = ds_new(d);
    if (!d->root) { free(d); return NULL; }
    return d;
}

void str_dawg_destroy(str_dawg_t* d) {
    if (!d) return;
    for (size_t i = 0; i < d->n_all; i++) {
        dawg_state_t* s = d->all[i];
        if (s) {
            free(s->labels);
            free(s->targets);
            free(s);
        }
    }
    free(d->all);
    free(d->prev_word);
    free(d);
}

ds_status_t str_dawg_add(str_dawg_t* d, const char* word) {
    if (!d || !word) return DS_ERR_INVALID;
    if (d->finished) return DS_ERR_INVALID;
    if (d->prev_word && strcmp(d->prev_word, word) >= 0) return DS_ERR_INVALID;

    size_t cp = d->prev_word ? common_prefix(d->prev_word, word) : 0;

    /* Walk from root along common prefix to find the last existing state. */
    dawg_state_t* cur = d->root;
    for (size_t i = 0; i < cp; i++) {
        /* last transition of cur should match word[i] (because insertion is sorted) */
        cur = cur->targets[cur->n_trans - 1];
    }

    /* Minimize the subtree we're about to leave. */
    if (d->prev_word && strlen(d->prev_word) > cp) {
        /* Run replace-or-register from deepest node of prev branch back to cur. */
        /* Build chain from cur down via last transitions */
        dawg_state_t* chain[1024];
        int depth = 0;
        dawg_state_t* p = cur;
        while (p->n_trans > 0 && depth < 1024) {
            chain[depth++] = p;
            p = p->targets[p->n_trans - 1];
        }
        for (int i = depth - 1; i >= 0; i--) {
            replace_or_register(d, chain[i]);
        }
    }

    /* Append new suffix nodes for word[cp..] */
    size_t wlen = strlen(word);
    for (size_t i = cp; i < wlen; i++) {
        dawg_state_t* nw = ds_new(d);
        if (!nw) return DS_ERR_NOMEM;
        if (ds_add_trans(cur, word[i], nw) < 0) return DS_ERR_NOMEM;
        cur = nw;
    }
    cur->terminal = 1;

    free(d->prev_word);
    d->prev_word = malloc(wlen + 1);
    if (!d->prev_word) return DS_ERR_NOMEM;
    memcpy(d->prev_word, word, wlen + 1);
    return DS_OK;
}

ds_status_t str_dawg_finish(str_dawg_t* d) {
    if (!d) return DS_ERR_INVALID;
    if (d->finished) return DS_OK;
    if (d->prev_word) {
        dawg_state_t* chain[1024];
        int depth = 0;
        dawg_state_t* p = d->root;
        while (p->n_trans > 0 && depth < 1024) {
            chain[depth++] = p;
            p = p->targets[p->n_trans - 1];
        }
        for (int i = depth - 1; i >= 0; i--) {
            replace_or_register(d, chain[i]);
        }
    }
    /* root itself counts as a state */
    d->state_count++;
    d->finished = 1;
    return DS_OK;
}

int str_dawg_contains(const str_dawg_t* d, const char* word) {
    if (!d || !word) return 0;
    const dawg_state_t* cur = d->root;
    for (const unsigned char* p = (const unsigned char*)word; *p; p++) {
        int found = 0;
        for (int i = 0; i < cur->n_trans; i++) {
            if ((unsigned char)cur->labels[i] == *p) {
                cur = cur->targets[i];
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    return cur->terminal ? 1 : 0;
}

size_t str_dawg_states(const str_dawg_t* d) {
    if (!d) return 0;
    return d->state_count;
}
