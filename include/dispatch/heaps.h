/* heaps.h — priority queue variants for libdispatch. */
#ifndef DISPATCH_HEAPS_H
#define DISPATCH_HEAPS_H

#include "dispatch/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Fibonacci heap ---------------- */
typedef struct heap_fib       heap_fib_t;
typedef struct heap_fib_node  heap_fib_node_t;

DS_API heap_fib_t*      heap_fib_create(void);
DS_API void             heap_fib_destroy(heap_fib_t*);
DS_API heap_fib_node_t* heap_fib_push(heap_fib_t*, ds_key_t, ds_val_t);
DS_API ds_status_t      heap_fib_peek_min(const heap_fib_t*, ds_entry_t* out);
DS_API ds_status_t      heap_fib_pop_min(heap_fib_t*, ds_entry_t* out);
DS_API ds_status_t      heap_fib_decrease_key(heap_fib_t*, heap_fib_node_t*, ds_key_t new_key);
DS_API size_t           heap_fib_size(const heap_fib_t*);

/* ---------------- Binomial heap ---------------- */
typedef struct heap_binom heap_binom_t;

DS_API heap_binom_t* heap_binom_create(void);
DS_API void          heap_binom_destroy(heap_binom_t*);
DS_API ds_status_t   heap_binom_push(heap_binom_t*, ds_key_t, ds_val_t);
DS_API ds_status_t   heap_binom_peek_min(const heap_binom_t*, ds_entry_t* out);
DS_API ds_status_t   heap_binom_pop_min(heap_binom_t*, ds_entry_t* out);
DS_API ds_status_t   heap_binom_merge(heap_binom_t* dst, heap_binom_t* src); /* src emptied */
DS_API size_t        heap_binom_size(const heap_binom_t*);

/* ---------------- Leftist tree ---------------- */
typedef struct heap_leftist heap_leftist_t;

DS_API heap_leftist_t* heap_leftist_create(void);
DS_API void            heap_leftist_destroy(heap_leftist_t*);
DS_API ds_status_t     heap_leftist_push(heap_leftist_t*, ds_key_t, ds_val_t);
DS_API ds_status_t     heap_leftist_peek_min(const heap_leftist_t*, ds_entry_t* out);
DS_API ds_status_t     heap_leftist_pop_min(heap_leftist_t*, ds_entry_t* out);
DS_API ds_status_t     heap_leftist_merge(heap_leftist_t* dst, heap_leftist_t* src);
DS_API size_t          heap_leftist_size(const heap_leftist_t*);

/* ---------------- Skew heap ---------------- */
typedef struct heap_skew heap_skew_t;

DS_API heap_skew_t* heap_skew_create(void);
DS_API void         heap_skew_destroy(heap_skew_t*);
DS_API ds_status_t  heap_skew_push(heap_skew_t*, ds_key_t, ds_val_t);
DS_API ds_status_t  heap_skew_peek_min(const heap_skew_t*, ds_entry_t* out);
DS_API ds_status_t  heap_skew_pop_min(heap_skew_t*, ds_entry_t* out);
DS_API ds_status_t  heap_skew_merge(heap_skew_t* dst, heap_skew_t* src);
DS_API size_t       heap_skew_size(const heap_skew_t*);

/* ---------------- Pairing heap ---------------- */
typedef struct heap_pairing heap_pairing_t;

DS_API heap_pairing_t* heap_pairing_create(void);
DS_API void            heap_pairing_destroy(heap_pairing_t*);
DS_API ds_status_t     heap_pairing_push(heap_pairing_t*, ds_key_t, ds_val_t);
DS_API ds_status_t     heap_pairing_peek_min(const heap_pairing_t*, ds_entry_t* out);
DS_API ds_status_t     heap_pairing_pop_min(heap_pairing_t*, ds_entry_t* out);
DS_API size_t          heap_pairing_size(const heap_pairing_t*);

/* ---------------- Double-ended priority queue ---------------- */
typedef struct depq depq_t;

DS_API depq_t*     depq_create(void);
DS_API void        depq_destroy(depq_t*);
DS_API ds_status_t depq_push(depq_t*, ds_key_t, ds_val_t);
DS_API ds_status_t depq_peek_min(const depq_t*, ds_entry_t* out);
DS_API ds_status_t depq_peek_max(const depq_t*, ds_entry_t* out);
DS_API ds_status_t depq_pop_min(depq_t*, ds_entry_t* out);
DS_API ds_status_t depq_pop_max(depq_t*, ds_entry_t* out);
DS_API size_t      depq_size(const depq_t*);

#ifdef __cplusplus
}
#endif

#endif /* DISPATCH_HEAPS_H */
