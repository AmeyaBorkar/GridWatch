/* trie.c — ASCII 128 fan-out trie */
#include "dispatch/strings.h"
#include <stdlib.h>
#include <string.h>

#define TRIE_FAN 128

/* ASCII Trie node: a fan-out array indexes one child per possible byte (here 128
 * for printable ASCII). `terminal` marks the end of an inserted word. This
 * straight-array layout gives O(L) lookup independent of the dictionary size,
 * which is what makes street-name autocomplete instantaneous in the dispatcher. */
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

/* The below block uses Trie insertion to walk the existing path letter-by-letter
 * and create a fresh chain of nodes for any missing suffix. Building the
 * dictionary this way lets later lookups reuse shared prefixes between street
 * names so the autocomplete index stays compact. */
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

/* Trie membership test: descend from the root following one byte per edge.
 * If any edge is missing the word is absent; otherwise it exists iff we land
 * on a terminal node. Used to validate that a typed street name is real. */
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
/* Trie completion enumeration: a depth-first walk from the prefix node that
 * appends every terminal it finds. The pre-order traversal yields completions
 * in lexicographic order, which is what the autocomplete dropdown shows. */
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

/* The below block uses Trie prefix lookup to first walk down the prefix path,
 * then DFS-collects all terminals beneath it. This is the heart of street-name
 * autocomplete: type "Mai" and get every street starting with those letters. */
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
