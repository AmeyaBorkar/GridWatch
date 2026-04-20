/* test_strings.c */
#include "dispatch/strings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* CITIES[50] = {
    "Abilene","Akron","Albany","Albuquerque","Alexandria","Allentown","Amarillo","Anaheim",
    "Anchorage","Arlington","Atlanta","Augusta","Aurora","Austin","Bakersfield","Baltimore",
    "Baton","Beaumont","Berkeley","Billings","Birmingham","Boise","Boston","Boulder",
    "Bridgeport","Brookings","Buffalo","Burbank","Cambridge","Carlsbad","Charleston","Charlotte",
    "Chattanooga","Chesapeake","Cheyenne","Chicago","Cincinnati","Cleveland","Colorado","Columbia",
    "Columbus","Concord","Corona","Corpus","Dallas","Davenport","Dayton","Denver","Detroit","Durham"
};

static void test_trie(void) {
    str_trie_t* t = str_trie_create();
    assert(t);
    for (int i = 0; i < 50; i++) {
        assert(str_trie_insert(t, CITIES[i], NULL) == DS_OK);
    }
    for (int i = 0; i < 50; i++) assert(str_trie_contains(t, CITIES[i]));
    assert(!str_trie_contains(t, "Zanzibar"));
    assert(!str_trie_contains(t, "Abilen"));

    char* out[32] = {0};
    size_t n = str_trie_prefix(t, "Ab", out, 32);
    /* Abilene */
    assert(n >= 1);
    int found_abilene = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(out[i], "Abilene") == 0) found_abilene = 1;
        free(out[i]);
    }
    assert(found_abilene);

    str_trie_destroy(t);
    printf("trie: OK (prefix 'Ab' => %zu results)\n", n);
}

static void test_crtrie(void) {
    static const char* W[30] = {
        "apple","app","application","apply","ape","banana","band","bandana","bandwidth","bear",
        "beast","beat","cat","cats","catapult","dog","dogma","dogs","door","doom",
        "elephant","elf","eel","fig","finch","fin","goose","grape","grove","gum"
    };
    str_crtrie_t* t = str_crtrie_create();
    assert(t);
    for (int i = 0; i < 30; i++) {
        ds_status_t s = str_crtrie_insert(t, W[i], NULL);
        assert(s == DS_OK);
    }
    for (int i = 0; i < 30; i++) {
        assert(str_crtrie_contains(t, W[i]));
    }
    assert(!str_crtrie_contains(t, "xyzzy"));
    assert(!str_crtrie_contains(t, "app "));
    assert(!str_crtrie_contains(t, "ban"));
    str_crtrie_destroy(t);
    printf("crtrie: OK\n");
}

static void test_sa(void) {
    const char* txt = "banana_bandana_canopy";
    size_t n = strlen(txt);
    str_sa_t* s = str_sa_build(txt, n);
    assert(s);
    assert(str_sa_contains(s, "ana", 3));
    assert(str_sa_count(s, "ana", 3) == 3); /* banAna, banAna(2nd a..), bandAna */
    assert(str_sa_contains(s, "banana", 6));
    assert(str_sa_count(s, "banana", 6) == 1);
    assert(!str_sa_contains(s, "zzz", 3));
    assert(str_sa_count(s, "an", 2) == 5);
    str_sa_destroy(s);

    /* plain banana */
    str_sa_t* s2 = str_sa_build("banana", 6);
    assert(s2);
    assert(str_sa_count(s2, "ana", 3) == 2);
    assert(str_sa_count(s2, "a", 1) == 3);
    str_sa_destroy(s2);
    printf("sa: OK\n");
}

static size_t count_raw_trie_nodes(const char** words, size_t n) {
    /* count distinct prefix nodes, i.e., unique prefixes */
    /* simple upper bound: sum of lengths + 1 */
    size_t total = 1;
    (void)n;
    for (size_t i = 0; words[i]; i++) total += strlen(words[i]);
    return total;
}

static void test_dawg(void) {
    str_dawg_t* d = str_dawg_create();
    assert(d);
    const char* words[] = {"cat","cats","dog", NULL};
    for (int i = 0; words[i]; i++) {
        assert(str_dawg_add(d, words[i]) == DS_OK);
    }
    assert(str_dawg_finish(d) == DS_OK);
    assert(str_dawg_contains(d, "cat"));
    assert(str_dawg_contains(d, "cats"));
    assert(str_dawg_contains(d, "dog"));
    assert(!str_dawg_contains(d, "ca"));
    assert(!str_dawg_contains(d, "dogs"));
    assert(!str_dawg_contains(d, "cow"));

    size_t states = str_dawg_states(d);
    size_t raw = count_raw_trie_nodes(words, 3);
    assert(states <= raw);
    printf("dawg: OK (states=%zu, raw<=%zu)\n", states, raw);
    str_dawg_destroy(d);
}

static void test_fuzzy(void) {
    static const char* W[30] = {
        "Birch","Beech","Maple","Oak","Pine","Cedar","Ash","Elm","Fir","Spruce",
        "Walnut","Willow","Hazel","Hickory","Poplar","Alder","Aspen","Chestnut","Cypress","Dogwood",
        "Ebony","Fig","Hemlock","Juniper","Laurel","Linden","Magnolia","Mahogany","Olive","Palm"
    };
    str_fuzzy_t* f = str_fuzzy_create();
    assert(f);
    for (int i = 0; i < 30; i++) {
        assert(str_fuzzy_add(f, W[i]) == DS_OK);
    }
    char* out[32] = {0};
    size_t n = str_fuzzy_search(f, "Birck", 2, out, 32);
    int found = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(out[i], "Birch") == 0) found = 1;
        free(out[i]);
    }
    assert(found);
    str_fuzzy_destroy(f);
    printf("fuzzy: OK (found Birch within 2 edits of 'Birck')\n");
}

int main(void) {
    test_trie();
    test_crtrie();
    test_sa();
    test_dawg();
    test_fuzzy();
    printf("ALL STRINGS TESTS PASSED\n");
    return 0;
}
