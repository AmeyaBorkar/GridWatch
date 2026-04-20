/* trie.c — ASCII 128 fan-out trie */
#include "dispatch/strings.h"
#include <stdlib.h>
#include <string.h>

#define TRIE_FAN 128

typedef struct trie_node {
    struct trie_node* kids[TRIE_FAN];
    ds_val_t val;
    int terminal;
} trie_node_t;

struct str_trie {
    trie_node_t* root;
};

static trie_node_t* node_new(void) {
    trie_node_t* n = calloc(1, sizeof(*n));
    return n;
}

static void node_free(trie_node_t* n) {
    if (!n) return;
    for (int i = 0; i < TRIE_FAN; i++) node_free(n->kids[i]);
    free(n);
}

str_trie_t* str_trie_create(void) {
    str_trie_t* t = malloc(sizeof(*t));
    if (!t) return NULL;
    t->root = node_new();
    if (!t->root) { free(t); return NULL; }
    return t;
}

void str_trie_destroy(str_trie_t* t) {
    if (!t) return;
    node_free(t->root);
    free(t);
}

ds_status_t str_trie_insert(str_trie_t* t, const char* word, ds_val_t v) {
    if (!t || !word) return DS_ERR_INVALID;
    trie_node_t* cur = t->root;
    for (const unsigned char* p = (const unsigned char*)word; *p; p++) {
        if (*p >= TRIE_FAN) return DS_ERR_INVALID;
        if (!cur->kids[*p]) {
            cur->kids[*p] = node_new();
            if (!cur->kids[*p]) return DS_ERR_NOMEM;
        }
        cur = cur->kids[*p];
    }
    cur->terminal = 1;
    cur->val = v;
    return DS_OK;
}

int str_trie_contains(const str_trie_t* t, const char* word) {
    if (!t || !word) return 0;
    const trie_node_t* cur = t->root;
    for (const unsigned char* p = (const unsigned char*)word; *p; p++) {
        if (*p >= TRIE_FAN || !cur->kids[*p]) return 0;
        cur = cur->kids[*p];
    }
    return cur->terminal ? 1 : 0;
}

/* DFS collect helper */
static void collect(const trie_node_t* n, char* buf, size_t depth, size_t cap,
                    char** out, size_t max, size_t* count) {
    if (!n || *count >= max) return;
    if (n->terminal) {
        char* s = malloc(depth + 1);
        if (s) {
            memcpy(s, buf, depth);
            s[depth] = '\0';
            out[(*count)++] = s;
        }
        if (*count >= max) return;
    }
    for (int i = 0; i < TRIE_FAN && *count < max; i++) {
        if (n->kids[i]) {
            if (depth + 1 >= cap) continue;
            buf[depth] = (char)i;
            collect(n->kids[i], buf, depth + 1, cap, out, max, count);
        }
    }
}

size_t str_trie_prefix(const str_trie_t* t, const char* prefix,
                       char** out, size_t max) {
    if (!t || !prefix || !out || max == 0) return 0;
    const trie_node_t* cur = t->root;
    size_t plen = strlen(prefix);
    for (size_t i = 0; i < plen; i++) {
        unsigned char c = (unsigned char)prefix[i];
        if (c >= TRIE_FAN || !cur->kids[c]) return 0;
        cur = cur->kids[c];
    }
    enum { BUFCAP = 1024 };
    char buf[BUFCAP];
    if (plen >= BUFCAP) return 0;
    memcpy(buf, prefix, plen);
    size_t count = 0;
    collect(cur, buf, plen, BUFCAP, out, max, &count);
    return count;
}
