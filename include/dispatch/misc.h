/* misc.h — miscellaneous data structures: DSU, persistent list, bit vector. */
#ifndef DISPATCH_MISC_H
#define DISPATCH_MISC_H

#include "dispatch/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Disjoint Set Union (union-by-rank + path compression). */
typedef struct misc_dsu misc_dsu_t;

DS_API misc_dsu_t* misc_dsu_create(size_t n);
DS_API void        misc_dsu_destroy(misc_dsu_t* d);
DS_API size_t      misc_dsu_find(misc_dsu_t* d, size_t x);
DS_API int         misc_dsu_union(misc_dsu_t* d, size_t a, size_t b);
DS_API size_t      misc_dsu_count(const misc_dsu_t* d);
DS_API size_t      misc_dsu_size(const misc_dsu_t* d, size_t x);

/* Persistent (immutable) cons list. */
typedef struct misc_plist misc_plist_t;
typedef struct misc_pnode misc_pnode_t;

DS_API misc_plist_t* misc_plist_create(void);
DS_API void          misc_plist_destroy(misc_plist_t* p);
DS_API misc_pnode_t* misc_plist_push(misc_plist_t* p, misc_pnode_t* head, ds_val_t v);
DS_API ds_status_t   misc_plist_head(const misc_pnode_t* n, ds_val_t* out);
DS_API misc_pnode_t* misc_plist_tail(const misc_pnode_t* n);
DS_API size_t        misc_plist_length(const misc_pnode_t* n);

/* Succinct bit vector with rank1/select1. */
typedef struct misc_bv misc_bv_t;

DS_API misc_bv_t* misc_bv_create(size_t n_bits);
DS_API void       misc_bv_destroy(misc_bv_t* b);
DS_API void       misc_bv_set(misc_bv_t* b, size_t idx, int value);
DS_API int        misc_bv_get(const misc_bv_t* b, size_t idx);
DS_API void       misc_bv_build(misc_bv_t* b);
DS_API size_t     misc_bv_rank1(const misc_bv_t* b, size_t idx);
DS_API size_t     misc_bv_select1(const misc_bv_t* b, size_t k);
DS_API size_t     misc_bv_size_bytes(const misc_bv_t* b);

#ifdef __cplusplus
}
#endif

#endif
