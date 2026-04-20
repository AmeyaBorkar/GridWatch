/* randomized.h — randomized data structures (skip list, treap). */
#ifndef DISPATCH_RANDOMIZED_H
#define DISPATCH_RANDOMIZED_H

#include <stddef.h>
#include <stdint.h>
#include "dispatch/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Seed the module-internal splitmix64 PRNG (used for level/priority choice). */
DS_API void rnd_seed(uint64_t s);

/* ---- Skip List (ordered map) ---- */
typedef struct rnd_skip_s rnd_skip_t;

DS_API rnd_skip_t* rnd_skip_create(void);
DS_API void        rnd_skip_destroy(rnd_skip_t*);
DS_API ds_status_t rnd_skip_insert(rnd_skip_t*, ds_key_t, ds_val_t);
DS_API ds_status_t rnd_skip_get(const rnd_skip_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t rnd_skip_delete(rnd_skip_t*, ds_key_t);
DS_API size_t      rnd_skip_size(const rnd_skip_t*);
/* Fill out[] with the k smallest entries in order. Returns # written. */
DS_API size_t      rnd_skip_top(const rnd_skip_t*, size_t k, ds_entry_t* out);

/* ---- Treap (BST with random priorities, split/merge) ---- */
typedef struct rnd_treap_s rnd_treap_t;

DS_API rnd_treap_t* rnd_treap_create(void);
DS_API void         rnd_treap_destroy(rnd_treap_t*);
DS_API ds_status_t  rnd_treap_insert(rnd_treap_t*, ds_key_t, ds_val_t);
DS_API ds_status_t  rnd_treap_get(const rnd_treap_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t  rnd_treap_delete(rnd_treap_t*, ds_key_t);
DS_API size_t       rnd_treap_size(const rnd_treap_t*);

#ifdef __cplusplus
}
#endif

#endif /* DISPATCH_RANDOMIZED_H */
