#include "sim_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initialise the rolling radio-log: a fixed-capacity character buffer plus
 * pointers to a Suffix Array and a Huffman tree built over its contents.
 * Both indexes start NULL — they are lazily (re)built as the log grows. */
void sim_log_init(sim_log_t* L) {
    L->cap = SIM_LOG_CAP;
    L->buf = (char*)malloc(L->cap);
    L->len = 0;
    if (L->buf) L->buf[0] = '\0';
    L->writes_since_sa = 0;
    L->writes_since_huff = 0;
    L->huff = NULL;
    L->sa = NULL;
}

void sim_log_free(sim_log_t* L) {
    free(L->buf); L->buf = NULL;
    if (L->huff) { huffman_destroy(L->huff); L->huff = NULL; }
    if (L->sa)   { str_sa_destroy(L->sa); L->sa = NULL; }
}

/* The below block uses a Rolling (FIFO) Buffer to keep only the most recent
 * cap-1 bytes of the radio log. When the new write would overflow, the oldest
 * bytes are shifted out via memmove. Bounded memory means the simulator can
 * run indefinitely without the log growing without limit. */
static void log_append_raw(sim_log_t* L, const char* s, size_t n) {
    if (!L->buf) return;
    /* Rolling buffer: keep last (cap-1) bytes when overflowing. */
    if (n >= L->cap - 1) {
        memcpy(L->buf, s + (n - (L->cap - 1)), L->cap - 1);
        L->len = L->cap - 1;
        L->buf[L->len] = '\0';
        return;
    }
    if (L->len + n >= L->cap) {
        size_t drop = L->len + n - (L->cap - 1);
        memmove(L->buf, L->buf + drop, L->len - drop);
        L->len -= drop;
    }
    memcpy(L->buf + L->len, s, n);
    L->len += n;
    L->buf[L->len] = '\0';
}

/* The below block appends one log line and then triggers a periodic rebuild
 * of the Suffix Array (every SIM_SA_REBUILD_EVERY writes) and the Huffman
 * code (every SIM_HUFF_REBUILD_EVERY writes). Both indexes go stale as the
 * underlying buffer mutates, so we amortise the cost rather than rebuilding
 * on every single write. */
void sim_log_append(struct sim* S, const char* fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(tmp)) n = (int)sizeof(tmp) - 1;

    log_append_raw(&S->log, tmp, (size_t)n);
    S->log.writes_since_sa++;
    S->log.writes_since_huff++;

    if (S->log.writes_since_sa >= SIM_SA_REBUILD_EVERY && S->log.len > 0) {
        if (S->log.sa) { str_sa_destroy(S->log.sa); S->log.sa = NULL; }
        S->log.sa = str_sa_build(S->log.buf, S->log.len);
        S->log.writes_since_sa = 0;
    }
    if (S->log.writes_since_huff >= SIM_HUFF_REBUILD_EVERY && S->log.len > 0) {
        if (S->log.huff) { huffman_destroy(S->log.huff); S->log.huff = NULL; }
        S->log.huff = huffman_build((const uint8_t*)S->log.buf, S->log.len);
        S->log.writes_since_huff = 0;
    }
}
