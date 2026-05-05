/* huffman.c — canonical-style Huffman coder over bytes. */
#include "dispatch/trees.h"
#include <stdlib.h>
#include <string.h>

#define HUF_ALPHA 256

/* Huffman tree node: leaves carry a byte symbol, internal nodes have symbol=-1
 * and two children. The greedy build merges the two lowest-freq nodes into a
 * new internal node until only the root remains. */
typedef struct hnode {
    uint32_t freq;
    int symbol;    /* -1 for internal */
    struct hnode* left;
    struct hnode* right;
} hnode_t;

/* Compressor state: the tree (for decode) plus a precomputed lookup table
 * mapping each byte -> (variable-length code, code length). The table is
 * what makes encode O(1) per symbol instead of walking the tree every time. */
struct huffman {
    hnode_t* root;
    uint64_t codes[HUF_ALPHA];
    uint8_t  code_len[HUF_ALPHA];
    int      used[HUF_ALPHA];
    int      n_symbols;
};

/* Binary min-heap (array-backed) keyed by frequency. This is the priority
 * queue that drives Huffman's greedy build — repeatedly fuse the two lightest
 * nodes. A simple binary heap suffices: only push and pop-min are needed. */
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

/* The below block walks the tree to derive the per-symbol bit codes:
 * left child = append 0, right child = append 1. The path from root to
 * leaf becomes that symbol's prefix-free code. Stored into h->codes for
 * fast lookup during encoding. */
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

/* The below block builds the Huffman tree via the classic greedy algorithm.
 * Step 1: count byte frequencies. Step 2: seed the PQ with one leaf per used
 * symbol. Step 3: repeatedly pop the two lightest, fuse into a parent, push
 * back. The last node out is the root. Used here to compress the radio log. */
huffman_t* huffman_build(const uint8_t* data, size_t n) {
    if (!data && n > 0) return NULL;
    huffman_t* h = (huffman_t*)calloc(1, sizeof *h);
    if (!h) return NULL;

    /* Frequency counting: a single pass to build the symbol histogram that
     * drives the greedy choice of which nodes to merge first. */
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
        /* The greedy build loop: pop the two least-frequent nodes, fuse them
         * into a parent whose freq is the sum, push back. After n-1 fusions
         * one tree remains — the optimal prefix code tree. */
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

/* The below block encodes via a precomputed lookup table: for each input
 * byte, fetch its bit code and length and emit the bits. O(1) per symbol —
 * no tree walks. This is the hot path for compressing the live radio log. */
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

/* The below block decodes by walking the tree one bit at a time:
 * 0 -> left child, 1 -> right child. On reaching a leaf, emit its symbol
 * and reset to root. The prefix-free property guarantees unambiguous parsing. */
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
