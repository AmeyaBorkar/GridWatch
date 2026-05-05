#include "sim_internal.h"
#include <stdlib.h>
#include <string.h>

static int cmp_strptr(const void* a, const void* b) {
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    return strcmp(*sa, *sb);
}

/* The below block wires the four string indexes into the simulator at startup:
 * an ASCII Trie and a Compressed Radix Trie (both for prefix autocomplete on
 * street names), a DAWG (compact membership recogniser for street tokens),
 * and a BK-tree (typo-tolerant fuzzy search). Each DS gets its own copy of
 * the dictionary so a query can be routed to whichever index fits the intent. */
int sim_search_init(struct sim* S) {
    S->trie   = str_trie_create();
    S->crtrie = str_crtrie_create();
    S->dawg   = str_dawg_create();
    S->fuzzy  = str_fuzzy_create();
    if (!S->trie || !S->crtrie || !S->dawg || !S->fuzzy) return -1;

    /* Insert individual street words (dedup for DAWG which needs sorted unique). */
    for (int i = 0; i < SIM_STREET_POOL_SZ; i++) {
        str_trie_insert(S->trie, SIM_STREETS[i], NULL);
        str_crtrie_insert(S->crtrie, SIM_STREETS[i], NULL);
        str_fuzzy_add(S->fuzzy, SIM_STREETS[i]);
    }

    /* Insert canonical node street names. */
    for (size_t i = 0; i < S->n_nodes; i++) {
        str_trie_insert(S->trie, S->nodes[i].street, NULL);
        str_crtrie_insert(S->crtrie, S->nodes[i].street, NULL);
        str_fuzzy_add(S->fuzzy, S->nodes[i].street);
    }

    /* DAWG: needs sorted unique input (the individual street words). */
    /* DAWG construction requires lexicographically-sorted, deduplicated input
     * because its incremental minimisation only finalises a branch once it
     * knows no later word will descend into it. */
    const char* sorted[SIM_STREET_POOL_SZ];
    for (int i = 0; i < SIM_STREET_POOL_SZ; i++) sorted[i] = SIM_STREETS[i];
    qsort(sorted, SIM_STREET_POOL_SZ, sizeof(sorted[0]), cmp_strptr);
    for (int i = 0; i < SIM_STREET_POOL_SZ; i++) {
        if (i > 0 && strcmp(sorted[i], sorted[i - 1]) == 0) continue;
        str_dawg_add(S->dawg, sorted[i]);
    }
    str_dawg_finish(S->dawg);

    return 0;
}

void sim_search_free(struct sim* S) {
    if (S->trie)   { str_trie_destroy(S->trie);   S->trie   = NULL; }
    if (S->crtrie) { str_crtrie_destroy(S->crtrie); S->crtrie = NULL; }
    if (S->dawg)   { str_dawg_destroy(S->dawg);   S->dawg   = NULL; }
    if (S->fuzzy)  { str_fuzzy_destroy(S->fuzzy); S->fuzzy  = NULL; }
}
