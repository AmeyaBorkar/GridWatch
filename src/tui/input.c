#include "tui.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void tui_state_init(tui_state_t* st) {
    memset(st, 0, sizeof(*st));
    st->search_mode = TUI_SEARCH_PREFIX;
    st->flash_node  = -1;
    st->last_spawn_rate = 0.4;
}

void tui_state_reset_completions(tui_state_t* st) {
    for (size_t i = 0; i < st->n_completions; ++i) {
        free(st->completions[i]);
        st->completions[i] = NULL;
    }
    st->n_completions = 0;
    st->selected_completion = -1;
}

void tui_update_completions(const sim_t* sim, tui_state_t* st) {
    tui_state_reset_completions(st);
    if (st->search_len == 0 || !sim) return;
    size_t n;
    if (st->search_mode == TUI_SEARCH_FUZZY) {
        n = sim_fuzzy(sim, st->search, 2, st->completions, TUI_MAX_COMPLETE);
    } else {
        n = sim_autocomplete(sim, st->search, st->completions, TUI_MAX_COMPLETE);
    }
    st->n_completions = n;
    st->selected_completion = (n > 0) ? 0 : -1;
}

static int is_search_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '&' || c == ' ';
}

void tui_handle_input(sim_t* sim, tui_state_t* st, double* spawn_rate) {
    while (tui_kbhit()) {
        int c = tui_getch();
        if (c < 0) continue;

        if (c == 27) { st->quit = 1; return; }
        if (c == 'q' || c == 'Q') { st->quit = 1; return; }

        if (c == ' ') {
            sim_set_paused(sim, !sim_is_paused(sim));
            continue;
        }
        if (c == 's' || c == 'S') {
            sim_force_spawn(sim);
            continue;
        }
        if (c == '+' || c == '=') {
            *spawn_rate += 0.1;
            if (*spawn_rate > 5.0) *spawn_rate = 5.0;
            sim_set_spawn_rate(sim, *spawn_rate);
            continue;
        }
        if (c == '-' || c == '_') {
            *spawn_rate -= 0.1;
            if (*spawn_rate < 0.0) *spawn_rate = 0.0;
            sim_set_spawn_rate(sim, *spawn_rate);
            continue;
        }
        if (c == '\t') {
            st->search_mode = (st->search_mode == TUI_SEARCH_PREFIX)
                              ? TUI_SEARCH_FUZZY : TUI_SEARCH_PREFIX;
            tui_update_completions(sim, st);
            continue;
        }
        if (c == '\b' || c == 127) {
            if (st->search_len > 0) {
                st->search[--st->search_len] = '\0';
                tui_update_completions(sim, st);
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            const char* target = NULL;
            if (st->selected_completion >= 0
                && (size_t)st->selected_completion < st->n_completions) {
                target = st->completions[st->selected_completion];
            } else if (st->search_len > 0) {
                target = st->search;
            }
            if (target) {
                int node = sim_node_by_street(sim, target);
                if (node >= 0) {
                    sim_metrics_t m; sim_metrics(sim, &m);
                    st->flash_node   = node;
                    st->flash_until  = m.sim_time + 1.5;
                }
            }
            continue;
        }
        if (is_search_char(c)) {
            if (st->search_len + 1 < TUI_SEARCH_CAP) {
                st->search[st->search_len++] = (char)c;
                st->search[st->search_len] = '\0';
                tui_update_completions(sim, st);
            }
            continue;
        }
    }
}
