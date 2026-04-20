/* huffman.c — canonical-style Huffman coder over bytes. */
#include "dispatch/trees.h"
#include <stdlib.h>
#include <string.h>

#define HUF_ALPHA 256

typedef struct hnode {
    uint32_t freq;
    int symbol;    /* -1 for internal */
    struct hnode* left;
    struct hnode* right;
} hnode_t;

struct huffman {
    hnode_t* root;
    uint64_t codes[HUF_ALPHA];
    uint8_t  code_len[HUF_ALPHA];
    int      used[HUF_ALPHA];
    int      n_symbols;
};

/* --- tiny binary heap of hnode_t* keyed by freq. --- */
typedef struct { hnode_t** a; size_t n, cap; } pq_t;

static int pq_init(pq_t* q, size_t cap) {
    q->a = (hnode_t**)malloc(sizeof(hnode_t*) * cap);
    if (!q->a) return -1;
    q->n = 0; q->cap = cap;
    return 0;
}
static void pq_free(pq_t* q) { free(q->a); }
static void pq_push(pq_t* q, hnode_t* x) {
    size_t i = q->n++;
    q->a[i] = x;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (q->a[p]->freq <= q->a[i]->freq) break;
        hnode_t* t = q->a[p]; q->a[p] = q->a[i]; q->a[i] = t;
        i = p;
    }
}
static hnode_t* pq_pop(pq_t* q) {
    hnode_t* top = q->a[0];
    q->a[0] = q->a[--q->n];
    size_t i = 0;
    for (;;) {
        size_t l = 2*i + 1, r = 2*i + 2, s = i;
        if (l < q->n && q->a[l]->freq < q->a[s]->freq) s = l;
        if (r < q->n && q->a[r]->freq < q->a[s]->freq) s = r;
        if (s == i) break;
        hnode_t* t = q->a[s]; q->a[s] = q->a[i]; q->a[i] = t;
        i = s;
    }
    return top;
}

static hnode_t* hnode_new(int sym, uint32_t f, hnode_t* l, hnode_t* r) {
    hnode_t* n = (hnode_t*)malloc(sizeof *n);
    if (!n) return NULL;
    n->symbol = sym; n->freq = f; n->left = l; n->right = r;
    return n;
}

static void hnode_free(hnode_t* n) {
    if (!n) return;
    hnode_free(n->left);
    hnode_free(n->right);
    free(n);
}

static int build_codes(huffman_t* h, hnode_t* n, uint64_t code, uint8_t len) {
    if (!n) return 0;
    if (n->symbol >= 0) {
        /* Leaf. If tree had only one distinct symbol, force len >= 1. */
        uint8_t effective = len == 0 ? 1 : len;
        if (effective > 64) return -1;
        h->codes[n->symbol] = code;
        h->code_len[n->symbol] = effective;
        h->used[n->symbol] = 1;
        return 0;
    }
    if (len >= 64) return -1;
    if (build_codes(h, n->left,  (code << 1) | 0, (uint8_t)(len + 1)) < 0) return -1;
    if (build_codes(h, n->right, (code << 1) | 1, (uint8_t)(len + 1)) < 0) return -1;
    return 0;
}

huffman_t* huffman_build(const uint8_t* data, size_t n) {
    if (!data && n > 0) return NULL;
    huffman_t* h = (huffman_t*)calloc(1, sizeof *h);
    if (!h) return NULL;

    uint32_t freq[HUF_ALPHA] = {0};
    for (size_t i = 0; i < n; i++) freq[data[i]]++;

    pq_t q;
    if (pq_init(&q, HUF_ALPHA + 1) < 0) { free(h); return NULL; }

    for (int s = 0; s < HUF_ALPHA; s++) {
        if (freq[s]) {
            hnode_t* leaf = hnode_new(s, freq[s], NULL, NULL);
            if (!leaf) { pq_free(&q); free(h); return NULL; }
            pq_push(&q, leaf);
            h->n_symbols++;
        }
    }

    if (q.n == 0) {
        /* empty input — no tree */
        pq_free(&q);
        h->root = NULL;
        return h;
    }
    if (q.n == 1) {
        /* single symbol — wrap in a parent so code length >= 1 */
        hnode_t* only = pq_pop(&q);
        h->root = hnode_new(-1, only->freq, only, NULL);
        if (!h->root) { hnode_free(only); pq_free(&q); free(h); return NULL; }
    } else {
        while (q.n > 1) {
            hnode_t* a = pq_pop(&q);
            hnode_t* b = pq_pop(&q);
            hnode_t* p = hnode_new(-1, a->freq + b->freq, a, b);
            if (!p) { hnode_free(a); hnode_free(b); pq_free(&q); huffman_destroy(h); return NULL; }
            pq_push(&q, p);
        }
        h->root = pq_pop(&q);
    }
    pq_free(&q);

    if (build_codes(h, h->root, 0, 0) < 0) {
        huffman_destroy(h);
        return NULL;
    }
    return h;
}

void huffman_destroy(huffman_t* h) {
    if (!h) return;
    hnode_free(h->root);
    free(h);
}

static void put_bit(uint8_t* out, size_t pos, int bit) {
    if (bit) out[pos >> 3] |=  (uint8_t)(1u << (7 - (pos & 7)));
    else     out[pos >> 3] &= (uint8_t)~(1u << (7 - (pos & 7)));
}
static int get_bit(const uint8_t* in, size_t pos) {
    return (in[pos >> 3] >> (7 - (pos & 7))) & 1;
}

size_t huffman_encode(const huffman_t* h, const uint8_t* in, size_t in_n,
                      uint8_t* out_bits, size_t out_cap) {
    if (!h || !in || !out_bits) return 0;
    /* zero bytes we'll touch */
    size_t total_bits = 0;
    for (size_t i = 0; i < in_n; i++) total_bits += h->code_len[in[i]];
    size_t need_bytes = (total_bits + 7) / 8;
    if (need_bytes > out_cap) return 0;
    memset(out_bits, 0, need_bytes);

    size_t pos = 0;
    for (size_t i = 0; i < in_n; i++) {
        uint8_t sym = in[i];
        uint8_t len = h->code_len[sym];
        uint64_t code = h->codes[sym];
        for (int b = len - 1; b >= 0; b--) {
            put_bit(out_bits, pos++, (int)((code >> b) & 1u));
        }
    }
    return pos;
}

size_t huffman_decode(const huffman_t* h, const uint8_t* in_bits, size_t bit_len,
                      uint8_t* out, size_t out_cap) {
    if (!h || !h->root || !in_bits || !out) return 0;
    size_t produced = 0;
    hnode_t* cur = h->root;
    /* Special case: tree with a single leaf that is also root (shouldn't
     * happen since we wrap, but guard anyway). */
    if (cur->symbol >= 0) {
        /* each bit emits that symbol; count = bit_len */
        if (bit_len > out_cap) return 0;
        for (size_t i = 0; i < bit_len; i++) out[i] = (uint8_t)cur->symbol;
        return bit_len;
    }
    for (size_t i = 0; i < bit_len; i++) {
        int bit = get_bit(in_bits, i);
        cur = bit ? cur->right : cur->left;
        if (!cur) return 0;
        if (cur->symbol >= 0) {
            if (produced >= out_cap) return 0;
            out[produced++] = (uint8_t)cur->symbol;
            cur = h->root;
        }
    }
    return produced;
}

double huffman_ratio(const huffman_t* h, const uint8_t* data, size_t n) {
    if (!h || n == 0) return 0.0;
    size_t bits = 0;
    for (size_t i = 0; i < n; i++) bits += h->code_len[data[i]];
    double compressed_bytes = (double)((bits + 7) / 8);
    return compressed_bytes / (double)n;
}
