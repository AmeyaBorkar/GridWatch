/* crtrie.c — compressed radix trie */
#include "dispatch/strings.h"
#include <stdlib.h>
#include <string.h>

/* Compressed Radix Trie node: instead of a per-byte fan-out, each edge carries
 * a multi-character `label` so single-child chains collapse into one node.
 * This drops memory dramatically when the dictionary has long shared
 * suffixes/prefixes — perfect for the dispatcher's street-name table. */
typedef struct cr_node {
    char* label;          /* edge label to this node (malloc) */
    size_t llen;
    struct cr_node** kids;
    size_t nkids;
    ds_val_t val;
    int terminal;
} cr_node_t;

struct str_crtrie {
    cr_node_t* root;
};

static cr_node_t* cr_node_new(const char* s, size_t n) {
    cr_node_t* x = calloc(1, sizeof(*x));
    if (!x) return NULL;
    if (n > 0) {
        x->label = malloc(n);
        if (!x->label) { free(x); return NULL; }
        memcpy(x->label, s, n);
        x->llen = n;
    }
    return x;
}

static void cr_node_free(cr_node_t* n) {
    if (!n) return;
    for (size_t i = 0; i < n->nkids; i++) cr_node_free(n->kids[i]);
    free(n->kids);
    free(n->label);
    free(n);
}

static cr_node_t* find_child(cr_node_t* p, char c) {
    for (size_t i = 0; i < p->nkids; i++) {
        if (p->kids[i]->llen > 0 && p->kids[i]->label[0] == c) return p->kids[i];
    }
    return NULL;
}

static int add_child(cr_node_t* p, cr_node_t* c) {
    cr_node_t** nk = realloc(p->kids, (p->nkids + 1) * sizeof(*nk));
    if (!nk) return -1;
    p->kids = nk;
    p->kids[p->nkids++] = c;
    return 0;
}

static void remove_child(cr_node_t* p, cr_node_t* c) {
    for (size_t i = 0; i < p->nkids; i++) {
        if (p->kids[i] == c) {
            for (size_t j = i + 1; j < p->nkids; j++) p->kids[j-1] = p->kids[j];
            p->nkids--;
            return;
        }
    }
}

str_crtrie_t* str_crtrie_create(void) {
    str_crtrie_t* t = malloc(sizeof(*t));
    if (!t) return NULL;
    t->root = cr_node_new(NULL, 0);
    if (!t->root) { free(t); return NULL; }
    return t;
}

void str_crtrie_destroy(str_crtrie_t* t) {
    if (!t) return;
    cr_node_free(t->root);
    free(t);
}

/* The below block uses Radix-Trie insertion with edge splitting. When the new
 * key only shares a prefix of an existing edge, the edge is broken into a
 * shared head plus two divergent tails — preserving the compressed invariant
 * (no node has exactly one child) while admitting the new word. */
ds_status_t str_crtrie_insert(str_crtrie_t* t, const char* word, ds_val_t v) {
    if (!t || !word) return DS_ERR_INVALID;
    const char* rem = word;
    size_t rlen = strlen(word);
    cr_node_t* cur = t->root;

    while (rlen > 0) {
        cr_node_t* child = find_child(cur, rem[0]);
        if (!child) {
            cr_node_t* nw = cr_node_new(rem, rlen);
            if (!nw) return DS_ERR_NOMEM;
            nw->terminal = 1;
            nw->val = v;
            if (add_child(cur, nw) < 0) { cr_node_free(nw); return DS_ERR_NOMEM; }
            return DS_OK;
        }
        /* find common prefix length */
        size_t cp = 0;
        size_t mx = child->llen < rlen ? child->llen : rlen;
        while (cp < mx && child->label[cp] == rem[cp]) cp++;

        if (cp == child->llen) {
            /* edge fully consumed */
            rem += cp;
            rlen -= cp;
            cur = child;
            if (rlen == 0) {
                child->terminal = 1;
                child->val = v;
                return DS_OK;
            }
        } else {
            /* split edge */
            cr_node_t* split = cr_node_new(child->label, cp);
            if (!split) return DS_ERR_NOMEM;
            /* shrink child's label */
            size_t new_llen = child->llen - cp;
            char* new_label = malloc(new_llen);
            if (!new_label) { cr_node_free(split); return DS_ERR_NOMEM; }
            memcpy(new_label, child->label + cp, new_llen);
            free(child->label);
            child->label = new_label;
            child->llen = new_llen;

            /* attach child under split */
            if (add_child(split, child) < 0) {
                free(new_label);
                child->label = NULL;
                cr_node_free(split);
                return DS_ERR_NOMEM;
            }
            /* replace child with split in cur */
            remove_child(cur, child);
            if (add_child(cur, split) < 0) return DS_ERR_NOMEM;

            rem += cp;
            rlen -= cp;
            if (rlen == 0) {
                split->terminal = 1;
                split->val = v;
                return DS_OK;
            }
            cr_node_t* nw = cr_node_new(rem, rlen);
            if (!nw) return DS_ERR_NOMEM;
            nw->terminal = 1;
            nw->val = v;
            if (add_child(split, nw) < 0) { cr_node_free(nw); return DS_ERR_NOMEM; }
            return DS_OK;
        }
    }
    cur->terminal = 1;
    cur->val = v;
    return DS_OK;
}

/* Radix-Trie search: at each node, pick the child whose edge label starts with
 * the next remaining char and consume the whole label in one step (memcmp).
 * Failing label match means absence — no per-character traversal needed. */
int str_crtrie_contains(const str_crtrie_t* t, const char* word) {
    if (!t || !word) return 0;
    const char* rem = word;
    size_t rlen = strlen(word);
    cr_node_t* cur = t->root;
    while (rlen > 0) {
        cr_node_t* child = find_child(cur, rem[0]);
        if (!child) return 0;
        if (rlen < child->llen) return 0;
        if (memcmp(child->label, rem, child->llen) != 0) return 0;
        rem += child->llen;
        rlen -= child->llen;
        cur = child;
    }
    return cur->terminal ? 1 : 0;
}
