/* bv.c — bit vector with block+superblock rank and select1 via binary search. */
#include "dispatch/misc.h"
#include <stdlib.h>
#include <string.h>

#define BV_WORD_BITS     64u
#define BV_BLOCK_WORDS   8u                              /* 512-bit block */
#define BV_BLOCK_BITS    (BV_WORD_BITS * BV_BLOCK_WORDS) /* 512 */
#define BV_SUPER_BLOCKS  8u                              /* 8 blocks per superblock */
#define BV_SUPER_BITS    (BV_BLOCK_BITS * BV_SUPER_BLOCKS) /* 4096 */

struct misc_bv {
    uint64_t* words;
    size_t    n_bits;
    size_t    n_words;
    /* rank index */
    uint32_t* block_rank;  /* rank within superblock, per block */
    size_t*   super_rank;  /* cumulative rank at start of each superblock */
    size_t    n_blocks;
    size_t    n_supers;
    size_t    total_ones;
    int       built;
};

static inline size_t word_popcount(uint64_t x) {
    /* portable 64-bit popcount */
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    return (size_t)((x * 0x0101010101010101ULL) >> 56);
}

DS_API misc_bv_t* misc_bv_create(size_t n_bits) {
    misc_bv_t* b = (misc_bv_t*)malloc(sizeof(*b));
    if (!b) return NULL;
    size_t nw = (n_bits + BV_WORD_BITS - 1) / BV_WORD_BITS;
    if (nw == 0) nw = 1;
    b->words = (uint64_t*)calloc(nw, sizeof(uint64_t));
    if (!b->words) { free(b); return NULL; }
    b->n_bits = n_bits;
    b->n_words = nw;
    b->block_rank = NULL;
    b->super_rank = NULL;
    b->n_blocks = 0;
    b->n_supers = 0;
    b->total_ones = 0;
    b->built = 0;
    return b;
}

DS_API void misc_bv_destroy(misc_bv_t* b) {
    if (!b) return;
    free(b->words);
    free(b->block_rank);
    free(b->super_rank);
    free(b);
}

DS_API void misc_bv_set(misc_bv_t* b, size_t idx, int value) {
    if (!b || idx >= b->n_bits) return;
    size_t w = idx / BV_WORD_BITS;
    size_t o = idx % BV_WORD_BITS;
    if (value) b->words[w] |=  ((uint64_t)1 << o);
    else       b->words[w] &= ~((uint64_t)1 << o);
    b->built = 0;
}

DS_API int misc_bv_get(const misc_bv_t* b, size_t idx) {
    if (!b || idx >= b->n_bits) return 0;
    size_t w = idx / BV_WORD_BITS;
    size_t o = idx % BV_WORD_BITS;
    return (int)((b->words[w] >> o) & 1u);
}

DS_API void misc_bv_build(misc_bv_t* b) {
    if (!b) return;
    size_t n_blocks = (b->n_bits + BV_BLOCK_BITS - 1) / BV_BLOCK_BITS;
    size_t n_supers = (b->n_bits + BV_SUPER_BITS - 1) / BV_SUPER_BITS;
    if (n_blocks == 0) n_blocks = 1;
    if (n_supers == 0) n_supers = 1;

    free(b->block_rank);
    free(b->super_rank);
    b->block_rank = (uint32_t*)calloc(n_blocks, sizeof(uint32_t));
    b->super_rank = (size_t*)calloc(n_supers + 1, sizeof(size_t));
    b->n_blocks = n_blocks;
    b->n_supers = n_supers;

    size_t total = 0;
    for (size_t s = 0; s < n_supers; s++) {
        b->super_rank[s] = total;
        size_t super_ones = 0;
        for (size_t bl = 0; bl < BV_SUPER_BLOCKS; bl++) {
            size_t block_idx = s * BV_SUPER_BLOCKS + bl;
            if (block_idx >= n_blocks) break;
            b->block_rank[block_idx] = (uint32_t)super_ones;
            size_t word_start = block_idx * BV_BLOCK_WORDS;
            for (size_t w = 0; w < BV_BLOCK_WORDS; w++) {
                size_t wi = word_start + w;
                if (wi >= b->n_words) break;
                super_ones += word_popcount(b->words[wi]);
            }
        }
        total += super_ones;
    }
    b->super_rank[n_supers] = total;
    b->total_ones = total;
    b->built = 1;
}

DS_API size_t misc_bv_rank1(const misc_bv_t* b, size_t idx) {
    if (!b || !b->built) return 0;
    if (idx >= b->n_bits) return b->total_ones;
    if (idx == 0) return 0;

    size_t super = idx / BV_SUPER_BITS;
    size_t block = idx / BV_BLOCK_BITS;
    size_t r = b->super_rank[super] + b->block_rank[block];

    size_t word_start = block * BV_BLOCK_WORDS;
    size_t bit_in_block = idx - block * BV_BLOCK_BITS;
    size_t full_words = bit_in_block / BV_WORD_BITS;
    size_t rem_bits   = bit_in_block % BV_WORD_BITS;

    for (size_t w = 0; w < full_words; w++) {
        size_t wi = word_start + w;
        if (wi >= b->n_words) return r;
        r += word_popcount(b->words[wi]);
    }
    if (rem_bits) {
        size_t wi = word_start + full_words;
        if (wi < b->n_words) {
            uint64_t mask = (rem_bits == 64) ? ~(uint64_t)0
                                             : (((uint64_t)1 << rem_bits) - 1);
            r += word_popcount(b->words[wi] & mask);
        }
    }
    return r;
}

DS_API size_t misc_bv_select1(const misc_bv_t* b, size_t k) {
    if (!b || !b->built) return (size_t)-1;
    if (k >= b->total_ones) return (size_t)-1;

    /* Binary search over superblocks on super_rank[s] <= k. */
    size_t lo = 0, hi = b->n_supers;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (b->super_rank[mid] <= k) lo = mid; else hi = mid;
    }
    size_t super = lo;
    size_t remaining = k - b->super_rank[super];

    /* Linear over up to 8 blocks. */
    size_t block = super * BV_SUPER_BLOCKS;
    size_t super_end = block + BV_SUPER_BLOCKS;
    if (super_end > b->n_blocks) super_end = b->n_blocks;
    while (block + 1 < super_end && b->block_rank[block + 1] <= remaining) block++;
    remaining -= b->block_rank[block];

    /* Linear over up to 8 words. */
    size_t word_start = block * BV_BLOCK_WORDS;
    size_t w = word_start;
    while (w < b->n_words) {
        size_t c = word_popcount(b->words[w]);
        if (c > remaining) break;
        remaining -= c;
        w++;
    }
    if (w >= b->n_words) return (size_t)-1;

    /* Find the (remaining)-th set bit in this word. */
    uint64_t x = b->words[w];
    size_t bit = 0;
    while (remaining > 0 || !(x & 1)) {
        if (x & 1) remaining--;
        x >>= 1;
        bit++;
        if (bit >= BV_WORD_BITS) return (size_t)-1;
    }
    size_t idx = w * BV_WORD_BITS + bit;
    if (idx >= b->n_bits) return (size_t)-1;
    return idx;
}

DS_API size_t misc_bv_size_bytes(const misc_bv_t* b) {
    if (!b) return 0;
    size_t s = sizeof(*b);
    s += b->n_words * sizeof(uint64_t);
    s += b->n_blocks * sizeof(uint32_t);
    s += (b->n_supers + 1) * sizeof(size_t);
    return s;
}
