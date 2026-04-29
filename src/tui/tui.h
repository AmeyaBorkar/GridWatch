#ifndef DISPATCH_TUI_H
#define DISPATCH_TUI_H

#include <stddef.h>
#include "dispatch/sim.h"

#define TUI_MAX_NODES     256
#define TUI_MAX_ROADS     512
#define TUI_MAX_STATIONS  32
#define TUI_MAX_UNITS     128
#define TUI_MAX_INCIDENTS 128
#define TUI_MAX_COMPLETE  8
#define TUI_SEARCH_CAP    48
#define TUI_LOG_CAP       2048
#define TUI_SCREEN_COLS   96
#define TUI_SCREEN_ROWS   32
#define TUI_BUF_CAP       (64 * 1024)

typedef enum { TUI_SEARCH_PREFIX = 0, TUI_SEARCH_FUZZY = 1 } tui_search_mode_t;

typedef struct {
    char              search[TUI_SEARCH_CAP];
    size_t            search_len;
    tui_search_mode_t search_mode;
    char*             completions[TUI_MAX_COMPLETE];
    size_t            n_completions;
    int               selected_completion;
    int               flash_node;
    double            flash_until;
    double            last_spawn_rate;
    int               help_visible;
    int               quit;
} tui_state_t;

void tui_console_init(void);
void tui_console_shutdown(void);
int  tui_kbhit(void);
int  tui_getch(void);

void tui_state_init(tui_state_t* st);
void tui_state_reset_completions(tui_state_t* st);
void tui_update_completions(const sim_t* sim, tui_state_t* st);

void tui_render_frame(const sim_t* sim, const tui_state_t* st, double spawn_rate);

void tui_handle_input(sim_t* sim, tui_state_t* st, double* spawn_rate);

#endif
