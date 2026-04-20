/* strings.h — string data structures */
#ifndef DISPATCH_STRINGS_H
#define DISPATCH_STRINGS_H

#include "dispatch/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Trie (ASCII 128 fan-out) ---------- */
typedef struct str_trie str_trie_t;

DS_API str_trie_t* str_trie_create(void);
DS_API void        str_trie_destroy(str_trie_t*);
DS_API ds_status_t str_trie_insert(str_trie_t*, const char* word, ds_val_t);
DS_API int         str_trie_contains(const str_trie_t*, const char* word);
/* Fill out (up to max) with malloc'd completions; caller frees each. */
DS_API size_t      str_trie_prefix(const str_trie_t*, const char* prefix,
                                   char** out, size_t max);

/* ---------- Compressed Radix Trie ---------- */
typedef struct str_crtrie str_crtrie_t;

DS_API str_crtrie_t* str_crtrie_create(void);
DS_API void          str_crtrie_destroy(str_crtrie_t*);
DS_API ds_status_t   str_crtrie_insert(str_crtrie_t*, const char* word, ds_val_t);
DS_API int           str_crtrie_contains(const str_crtrie_t*, const char* word);

/* ---------- Suffix Array ---------- */
typedef struct str_sa str_sa_t;

DS_API str_sa_t* str_sa_build(const char* text, size_t n);
DS_API void      str_sa_destroy(str_sa_t*);
DS_API int       str_sa_contains(const str_sa_t*, const char* pattern, size_t m);
DS_API size_t    str_sa_count(const str_sa_t*, const char* pattern, size_t m);

/* ---------- DAWG (minimal automaton, sorted input) ---------- */
typedef struct str_dawg str_dawg_t;

DS_API str_dawg_t* str_dawg_create(void);
DS_API void        str_dawg_destroy(str_dawg_t*);
DS_API ds_status_t str_dawg_add(str_dawg_t*, const char* word);
DS_API ds_status_t str_dawg_finish(str_dawg_t*);
DS_API int         str_dawg_contains(const str_dawg_t*, const char* word);
DS_API size_t      str_dawg_states(const str_dawg_t*);

/* ---------- Fuzzy Dict (BK-tree, Levenshtein) ---------- */
typedef struct str_fuzzy str_fuzzy_t;

DS_API str_fuzzy_t* str_fuzzy_create(void);
DS_API void         str_fuzzy_destroy(str_fuzzy_t*);
DS_API ds_status_t  str_fuzzy_add(str_fuzzy_t*, const char* word);
/* Caller frees each out[i]. */
DS_API size_t       str_fuzzy_search(const str_fuzzy_t*, const char* q,
                                     int max_edits, char** out, size_t max);

#ifdef __cplusplus
}
#endif

#endif
