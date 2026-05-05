/* sa.c — suffix array via prefix doubling, LCP via Kasai (not used externally yet) */
#include "dispatch/strings.h"
#include <stdlib.h>
#include <string.h>

/* Suffix Array bundle: `sa` holds the indices of every suffix of `text` sorted
 * lexicographically, and `lcp` holds the longest-common-prefix between
 * adjacent suffixes in that order. Together they support fast substring
 * queries over the radio log. */
struct str_sa {
    char* text;     /* owned copy */
    size_t n;
    int* sa;        /* size n */
    int* lcp;       /* size n, Kasai */
};

typedef struct { int r0, r1, idx; } pd_t;

static int pd_cmp(const void* a, const void* b) {
    const pd_t* x = a; const pd_t* y = b;
    if (x->r0 != y->r0) return x->r0 < y->r0 ? -1 : 1;
    if (x->r1 != y->r1) return x->r1 < y->r1 ? -1 : 1;
    return 0;
}

/* The below block uses Prefix-Doubling Suffix-Array construction. At step k
 * each suffix is represented by the rank of its first k chars and the rank of
 * its k-th-shifted suffix; sorting these (rank, rank) pairs doubles the
 * resolved prefix length each round, giving O(n log^2 n) construction. */
static int build_sa(const unsigned char* s, size_t n, int* sa) {
    if (n == 0) return 0;
    int* rank = malloc(n * sizeof(int));
    int* tmp = malloc(n * sizeof(int));
    pd_t* arr = malloc(n * sizeof(pd_t));
    if (!rank || !tmp || !arr) { free(rank); free(tmp); free(arr); return -1; }

    for (size_t i = 0; i < n; i++) rank[i] = s[i];

    for (size_t k = 1;; k *= 2) {
        for (size_t i = 0; i < n; i++) {
            arr[i].r0 = rank[i];
            arr[i].r1 = (i + k < n) ? rank[i + k] : -1;
            arr[i].idx = (int)i;
        }
        qsort(arr, n, sizeof(pd_t), pd_cmp);
        tmp[arr[0].idx] = 0;
        for (size_t i = 1; i < n; i++) {
            tmp[arr[i].idx] = tmp[arr[i-1].idx];
            if (arr[i].r0 != arr[i-1].r0 || arr[i].r1 != arr[i-1].r1)
                tmp[arr[i].idx]++;
        }
        for (size_t i = 0; i < n; i++) rank[i] = tmp[i];
        if (rank[arr[n-1].idx] == (int)n - 1) break;
        if (k >= n) break;
    }
    for (size_t i = 0; i < n; i++) sa[i] = arr[i].idx;

    free(rank); free(tmp); free(arr);
    return 0;
}

/* The below block uses Kasai's algorithm to compute the LCP array in O(n).
 * Trick: when we move from suffix i to suffix i+1, the LCP can drop by at most
 * 1, so the running counter `h` only ever decreases by one per step — keeping
 * total work linear instead of quadratic. */
static int build_lcp_kasai(const unsigned char* s, size_t n, const int* sa, int* lcp) {
    if (n == 0) return 0;
    int* inv = malloc(n * sizeof(int));
    if (!inv) return -1;
    for (size_t i = 0; i < n; i++) inv[sa[i]] = (int)i;
    int h = 0;
    for (size_t i = 0; i < n; i++) {
        if (inv[i] > 0) {
            int j = sa[inv[i] - 1];
            while (i + (size_t)h < n && (size_t)j + (size_t)h < n &&
                   s[i + h] == s[j + h]) h++;
            lcp[inv[i]] = h;
            if (h > 0) h--;
        } else {
            lcp[inv[i]] = 0;
        }
    }
    free(inv);
    return 0;
}

/* Build entry point: copies the text, runs the prefix-doubling sort, then the
 * Kasai LCP pass. Called periodically by the radio log when the buffer grows
 * enough to make the existing index stale. */
str_sa_t* str_sa_build(const char* text, size_t n) {
    if (!text) return NULL;
    str_sa_t* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->n = n;
    if (n == 0) {
        s->text = malloc(1);
        if (!s->text) { free(s); return NULL; }
        s->text[0] = '\0';
        return s;
    }
    s->text = malloc(n + 1);
    if (!s->text) { free(s); return NULL; }
    memcpy(s->text, text, n);
    s->text[n] = '\0';
    s->sa = malloc(n * sizeof(int));
    s->lcp = malloc(n * sizeof(int));
    if (!s->sa || !s->lcp) { str_sa_destroy(s); return NULL; }
    if (build_sa((const unsigned char*)s->text, n, s->sa) < 0) {
        str_sa_destroy(s); return NULL;
    }
    if (build_lcp_kasai((const unsigned char*)s->text, n, s->sa, s->lcp) < 0) {
        str_sa_destroy(s); return NULL;
    }
    return s;
}

void str_sa_destroy(str_sa_t* s) {
    if (!s) return;
    free(s->text);
    free(s->sa);
    free(s->lcp);
    free(s);
}

/* Compare pattern with suffix at sa[idx]; return <0 if pat<suf, 0 equal-prefix, >0 if pat>suf */
static int cmp_pat_suf(const char* text, size_t n, int suf, const char* pat, size_t m) {
    size_t i = 0;
    while (i < m && (size_t)suf + i < n) {
        unsigned char a = (unsigned char)pat[i];
        unsigned char b = (unsigned char)text[suf + i];
        if (a != b) return a < b ? -1 : 1;
        i++;
    }
    if (i == m) return 0;            /* pattern is a prefix of suffix */
    return 1;                         /* pattern longer than suffix -> pat > suf */
}

/* lower bound: first sa index where suffix >= pattern (as prefix match treated as equal) */
static size_t lb(const str_sa_t* s, const char* pat, size_t m) {
    size_t lo = 0, hi = s->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp_pat_suf(s->text, s->n, s->sa[mid], pat, m);
        if (c <= 0) hi = mid; else lo = mid + 1;
    }
    return lo;
}

/* upper bound: first sa index where suffix > pattern (pattern no longer prefix) */
static size_t ub(const str_sa_t* s, const char* pat, size_t m) {
    size_t lo = 0, hi = s->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp_pat_suf(s->text, s->n, s->sa[mid], pat, m);
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return lo;
}

/* The below block uses Suffix-Array binary search for substring queries.
 * Because suffixes are sorted, every occurrence of the pattern lies in a
 * contiguous SA range [lb, ub); a single lookup at lb is enough to confirm
 * presence. This powers fast "did anyone say X?" scans of the radio log. */
int str_sa_contains(const str_sa_t* s, const char* pattern, size_t m) {
    if (!s || !pattern || m == 0 || s->n == 0) return 0;
    size_t l = lb(s, pattern, m);
    if (l >= s->n) return 0;
    int suf = s->sa[l];
    if ((size_t)suf + m > s->n) return 0;
    return memcmp(s->text + suf, pattern, m) == 0 ? 1 : 0;
}

/* Suffix-Array occurrence count: the number of suffixes starting with the
 * pattern equals the size of the SA range [lb, ub). Lets the dispatcher count
 * how many times a phrase appeared in the log in O(m log n). */
size_t str_sa_count(const str_sa_t* s, const char* pattern, size_t m) {
    if (!s || !pattern || m == 0 || s->n == 0) return 0;
    size_t l = lb(s, pattern, m);
    size_t r = ub(s, pattern, m);
    if (r < l) return 0;
    return r - l;
}
