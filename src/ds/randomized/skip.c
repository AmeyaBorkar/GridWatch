/* skip.c — probabilistic skip list (max level 16, p=0.5). */
#include <stdlib.h>
#include <stdint.h>
#include "dispatch/randomized.h"

#define RND_SKIP_MAX_LEVEL 16

/* From rng.c */
uint64_t rnd__mix_u64(uint64_t* state);
uint64_t rnd__fresh_state(void);

/* The below block uses the SKIP LIST node layout: each node carries a key/value
 * plus an array of `level` forward pointers (one per level it was promoted to).
 * The flexible tail `forward[1]` is sized at allocation time so taller towers
 * cost more memory — this is what gives the structure its layered express-lane
 * shortcuts used by the ETA leaderboard. */
typedef struct skip_node_s {
    ds_key_t key;
    ds_val_t val;
    int      level;              /* number of forward pointers */
    struct skip_node_s* forward[1]; /* flexible tail */
} skip_node_t;

struct rnd_skip_s {
    skip_node_t* head;           /* sentinel with MAX_LEVEL forwards */
    int          level;          /* current max populated level (1..MAX) */
    size_t       size;
    uint64_t     rng;            /* per-instance PRNG state */
};

static skip_node_t* skip_node_alloc(int level, ds_key_t k, ds_val_t v) {
    size_t bytes = sizeof(skip_node_t) + sizeof(skip_node_t*) * (size_t)(level - 1);
    skip_node_t* n = (skip_node_t*)malloc(bytes);
    if (!n) return NULL;
    n->key = k;
    n->val = v;
    n->level = level;
    for (int i = 0; i < level; ++i) n->forward[i] = NULL;
    return n;
}

/* The below block uses SKIP LIST RANDOMIZATION (geometric distribution, p=0.5)
 * to decide a new node's tower height. Coin-flipping until tails gives expected
 * O(log n) layer count without any explicit balancing — this is what keeps the
 * leaderboard's insert/search expected-logarithmic. */
static int skip_random_level(uint64_t* state) {
    int lvl = 1;
    while (lvl < RND_SKIP_MAX_LEVEL) {
        uint64_t r = rnd__mix_u64(state);
        if ((r & 1ULL) == 0) break;
        ++lvl;
    }
    return lvl;
}

rnd_skip_t* rnd_skip_create(void) {
    rnd_skip_t* s = (rnd_skip_t*)malloc(sizeof(*s));
    if (!s) return NULL;
    s->head = skip_node_alloc(RND_SKIP_MAX_LEVEL, 0.0, NULL);
    if (!s->head) { free(s); return NULL; }
    s->level = 1;
    s->size = 0;
    s->rng = rnd__fresh_state();
    return s;
}

void rnd_skip_destroy(rnd_skip_t* s) {
    if (!s) return;
    skip_node_t* cur = s->head->forward[0];
    while (cur) {
        skip_node_t* nxt = cur->forward[0];
        free(cur);
        cur = nxt;
    }
    free(s->head);
    free(s);
}

/* The below block uses SKIP LIST INSERT: top-down level-walk that records the
 * `update[]` predecessor at each level, then splices a new tower of random
 * height into all those forward chains. Keeping the leaderboard sorted in
 * expected O(log n) per insert as ETAs change every tick. */
ds_status_t rnd_skip_insert(rnd_skip_t* s, ds_key_t k, ds_val_t v) {
    if (!s) return DS_ERR_INVALID;
    skip_node_t* update[RND_SKIP_MAX_LEVEL];
    skip_node_t* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {
        while (x->forward[i] && x->forward[i]->key < k) x = x->forward[i];
        update[i] = x;
    }
    x = x->forward[0];
    if (x && x->key == k) {
        x->val = v; /* update existing */
        return DS_OK;
    }
    int lvl = skip_random_level(&s->rng);
    if (lvl > s->level) {
        for (int i = s->level; i < lvl; ++i) update[i] = s->head;
        s->level = lvl;
    }
    skip_node_t* n = skip_node_alloc(lvl, k, v);
    if (!n) return DS_ERR_NOMEM;
    for (int i = 0; i < lvl; ++i) {
        n->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = n;
    }
    ++s->size;
    return DS_OK;
}

/* The below block uses SKIP LIST SEARCH: starts at the highest level and
 * drops down whenever the next forward pointer would overshoot, so the
 * upper express lanes skip large key ranges. Used to look up a unit's
 * current ETA entry in expected O(log n). */
ds_status_t rnd_skip_get(const rnd_skip_t* s, ds_key_t k, ds_val_t* out) {
    if (!s || !out) return DS_ERR_INVALID;
    const skip_node_t* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {
        while (x->forward[i] && x->forward[i]->key < k) x = x->forward[i];
    }
    x = x->forward[0];
    if (x && x->key == k) { *out = x->val; return DS_OK; }
    return DS_ERR_NOT_FOUND;
}

/* The below block uses SKIP LIST DELETE: same top-down predecessor walk as
 * insert/search, then unlinks the target tower from every level it appears in
 * and trims unused top levels. Lets the leaderboard evict resolved incidents. */
ds_status_t rnd_skip_delete(rnd_skip_t* s, ds_key_t k) {
    if (!s) return DS_ERR_INVALID;
    skip_node_t* update[RND_SKIP_MAX_LEVEL];
    skip_node_t* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {
        while (x->forward[i] && x->forward[i]->key < k) x = x->forward[i];
        update[i] = x;
    }
    x = x->forward[0];
    if (!x || x->key != k) return DS_ERR_NOT_FOUND;
    for (int i = 0; i < s->level; ++i) {
        if (update[i]->forward[i] != x) break;
        update[i]->forward[i] = x->forward[i];
    }
    free(x);
    while (s->level > 1 && s->head->forward[s->level - 1] == NULL) --s->level;
    --s->size;
    return DS_OK;
}

size_t rnd_skip_size(const rnd_skip_t* s) {
    return s ? s->size : 0;
}

/* The below block uses the SKIP LIST's bottom level (level 0), which is a
 * fully sorted linked list, to emit the smallest k keys in order. This is
 * what the UI calls to render the "top-N soonest ETAs" leaderboard. */
size_t rnd_skip_top(const rnd_skip_t* s, size_t k, ds_entry_t* out) {
    if (!s || !out || k == 0) return 0;
    size_t written = 0;
    const skip_node_t* x = s->head->forward[0];
    while (x && written < k) {
        out[written].key = x->key;
        out[written].val = x->val;
        ++written;
        x = x->forward[0];
    }
    return written;
}
