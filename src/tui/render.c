#include "tui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAP_COL0       1
#define MAP_ROW0       1
#define MAP_INNER_W    64
#define MAP_INNER_H    24
#define STATS_COL0     67
#define STATS_W        28
#define TOTAL_W        96
#define RADIO_ROW      26
#define RADIO_H        3
#define SEARCH_ROW     29
#define HINT_ROW       31

static char   g_buf[TUI_BUF_CAP];
static size_t g_len = 0;

static void buf_reset(void) { g_len = 0; }
static void buf_write(const char* s, size_t n) {
    if (g_len + n >= TUI_BUF_CAP) return;
    memcpy(g_buf + g_len, s, n);
    g_len += n;
}
static void buf_puts(const char* s) { buf_write(s, strlen(s)); }
static void buf_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    if (g_len >= TUI_BUF_CAP - 1) { va_end(ap); return; }
    int n = vsnprintf(g_buf + g_len, TUI_BUF_CAP - g_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_len += (size_t)n;
}

static void fg(int r, int g, int b) { buf_printf("\x1b[38;2;%d;%d;%dm", r, g, b); }
static void reset_sgr(void)         { buf_puts("\x1b[0m"); }
static void blink_on(void)          { buf_puts("\x1b[5m"); }
static void move_to(int row, int col) { buf_printf("\x1b[%d;%dH", row, col); }

/* cell grid: a cols,rows sim => inner grid width = cols*cw+1, height = rows*ch+1.
 * We pick cw/ch so that result <= MAP_INNER_W/H. */
typedef struct {
    int rows, cols;
    int cw, ch;          /* cell width/height in chars */
    int grid_w, grid_h;  /* computed inner grid size */
    int off_x, off_y;    /* offset inside map area for centering */
    char glyph[MAP_INNER_H][MAP_INNER_W + 1];
    int  fg_r[MAP_INNER_H][MAP_INNER_W];
    int  fg_g[MAP_INNER_H][MAP_INNER_W];
    int  fg_b[MAP_INNER_H][MAP_INNER_W];
    int  has_color[MAP_INNER_H][MAP_INNER_W];
    int  blink[MAP_INNER_H][MAP_INNER_W];
} map_t;

static void map_init(map_t* m, int rows, int cols) {
    m->rows = rows > 0 ? rows : 1;
    m->cols = cols > 0 ? cols : 1;
    int cw = (MAP_INNER_W - 1) / m->cols;
    int ch = (MAP_INNER_H - 1) / m->rows;
    if (cw < 2) cw = 2;
    if (ch < 1) ch = 1;
    if (cw > 6) cw = 6;
    if (ch > 3) ch = 3;
    m->cw = cw; m->ch = ch;
    m->grid_w = m->cols * cw + 1;
    m->grid_h = m->rows * ch + 1;
    if (m->grid_w > MAP_INNER_W) m->grid_w = MAP_INNER_W;
    if (m->grid_h > MAP_INNER_H) m->grid_h = MAP_INNER_H;
    m->off_x = (MAP_INNER_W - m->grid_w) / 2;
    m->off_y = (MAP_INNER_H - m->grid_h) / 2;
    for (int y = 0; y < MAP_INNER_H; ++y) {
        for (int x = 0; x < MAP_INNER_W; ++x) {
            m->glyph[y][x] = ' ';
            m->has_color[y][x] = 0;
            m->blink[y][x] = 0;
        }
        m->glyph[y][MAP_INNER_W] = '\0';
    }
}

static void map_set(map_t* m, int x, int y, char g, int r, int gg, int b, int blink) {
    if (x < 0 || x >= MAP_INNER_W || y < 0 || y >= MAP_INNER_H) return;
    m->glyph[y][x] = g;
    m->fg_r[y][x] = r;
    m->fg_g[y][x] = gg;
    m->fg_b[y][x] = b;
    m->has_color[y][x] = 1;
    m->blink[y][x] = blink;
}

static void map_node_to_screen(const map_t* m, int row, int col, int* sx, int* sy) {
    *sx = m->off_x + col * m->cw;
    *sy = m->off_y + row * m->ch;
}

static void map_world_to_screen(const map_t* m, double wx, double wy,
                                double span_x, double span_y,
                                double ox, double oy,
                                int* sx, int* sy) {
    double u = (span_x > 0) ? (wx - ox) / span_x : 0.0;
    double v = (span_y > 0) ? (wy - oy) / span_y : 0.0;
    int cells_x = (m->cols - 1);
    int cells_y = (m->rows - 1);
    if (cells_x < 1) cells_x = 1;
    if (cells_y < 1) cells_y = 1;
    int px = (int)(u * cells_x * m->cw + 0.5);
    int py = (int)(v * cells_y * m->ch + 0.5);
    *sx = m->off_x + px;
    *sy = m->off_y + py;
}

static void draw_box_borders(void) {
    /* Outer frame + panel divider. Drawn once; later we redraw every frame. */
    fg(140, 145, 160);
    move_to(1, 1);
    buf_puts("\xe2\x95\xad"); /* ╭ */
    for (int i = 0; i < MAP_INNER_W; ++i) buf_puts("\xe2\x94\x80"); /* ─ */
    buf_puts("\xe2\x94\xac"); /* ┬ */
    for (int i = 0; i < STATS_W; ++i)     buf_puts("\xe2\x94\x80");
    buf_puts("\xe2\x95\xae"); /* ╮ */

    for (int r = 2; r <= MAP_INNER_H + 1; ++r) {
        move_to(r, 1);
        buf_puts("\xe2\x94\x82");                       /* │ */
        move_to(r, MAP_INNER_W + 2);
        buf_puts("\xe2\x94\x82");
        move_to(r, TOTAL_W);
        buf_puts("\xe2\x94\x82");
    }

    move_to(MAP_INNER_H + 2, 1);
    buf_puts("\xe2\x94\x9c"); /* ├ */
    for (int i = 0; i < MAP_INNER_W; ++i) buf_puts("\xe2\x94\x80");
    buf_puts("\xe2\x94\xb4"); /* ┴ */
    for (int i = 0; i < STATS_W; ++i)     buf_puts("\xe2\x94\x80");
    buf_puts("\xe2\x94\xa4"); /* ┤ */

    for (int r = MAP_INNER_H + 3; r <= MAP_INNER_H + 5; ++r) {
        move_to(r, 1);         buf_puts("\xe2\x94\x82");
        move_to(r, TOTAL_W);   buf_puts("\xe2\x94\x82");
    }

    move_to(MAP_INNER_H + 6, 1);
    buf_puts("\xe2\x94\x9c");
    for (int i = 0; i < TOTAL_W - 2; ++i) buf_puts("\xe2\x94\x80");
    buf_puts("\xe2\x94\xa4");

    for (int r = MAP_INNER_H + 7; r <= MAP_INNER_H + 8; ++r) {
        move_to(r, 1);         buf_puts("\xe2\x94\x82");
        move_to(r, TOTAL_W);   buf_puts("\xe2\x94\x82");
    }

    move_to(MAP_INNER_H + 9, 1);
    buf_puts("\xe2\x95\xb0"); /* ╰ */
    for (int i = 0; i < TOTAL_W - 2; ++i) buf_puts("\xe2\x94\x80");
    buf_puts("\xe2\x95\xaf"); /* ╯ */

    /* Titles */
    fg(160, 220, 255);
    move_to(1, 4);
    buf_puts(" Emergency Dispatch Simulator ");
    move_to(1, MAP_INNER_W + 5);
    buf_puts(" Stats ");
    reset_sgr();
}

static char road_glyph_at(const map_t* m, int sx, int sy) {
    (void)m;
    char g = ' ';
    if (sx >= 0 && sx < MAP_INNER_W && sy >= 0 && sy < MAP_INNER_H) {
        g = m->glyph[sy][sx];
    }
    return g;
}

static void draw_road(map_t* m, int r0, int c0, int r1, int c1) {
    int sx0, sy0, sx1, sy1;
    map_node_to_screen(m, r0, c0, &sx0, &sy0);
    map_node_to_screen(m, r1, c1, &sx1, &sy1);
    int dx = (sx1 > sx0) ? 1 : (sx1 < sx0) ? -1 : 0;
    int dy = (sy1 > sy0) ? 1 : (sy1 < sy0) ? -1 : 0;
    int x = sx0, y = sy0;
    while (x != sx1 || y != sy1) {
        if (x == sx0 && y == sy0) { x += dx; y += dy; continue; }
        if (x == sx1 && y == sy1) break;
        char existing = road_glyph_at(m, x, y);
        char newg;
        if (dy == 0)      newg = '-';
        else if (dx == 0) newg = '|';
        else              newg = '+';
        if ((existing == '-' && newg == '|') || (existing == '|' && newg == '-')) newg = '+';
        else if (existing == '+') newg = '+';
        map_set(m, x, y, newg, 80, 80, 95, 0);
        x += dx;
        y += dy;
    }
}

static const char* glyph_to_utf8(char g) {
    switch (g) {
        case '-': return "\xe2\x94\x80"; /* ─ */
        case '|': return "\xe2\x94\x82"; /* │ */
        case '+': return "\xe2\x94\xbc"; /* ┼ */
        case '.': return "\xc2\xb7";     /* · */
        default:  return NULL;
    }
}

static void emit_map(const map_t* m) {
    for (int y = 0; y < MAP_INNER_H; ++y) {
        move_to(MAP_ROW0 + 1 + y, MAP_COL0 + 1);
        int last_r = -1, last_g = -1, last_b = -1, last_blink = 0;
        for (int x = 0; x < MAP_INNER_W; ++x) {
            char g = m->glyph[y][x];
            if (m->has_color[y][x]) {
                int r = m->fg_r[y][x], gg = m->fg_g[y][x], b = m->fg_b[y][x];
                if (r != last_r || gg != last_g || b != last_b) {
                    fg(r, gg, b);
                    last_r = r; last_g = gg; last_b = b;
                }
                if (m->blink[y][x] && !last_blink) { blink_on(); last_blink = 1; }
                if (!m->blink[y][x] && last_blink)  { buf_puts("\x1b[25m"); last_blink = 0; }
            } else {
                if (last_r != -1 || last_blink) { reset_sgr(); last_r = last_g = last_b = -1; last_blink = 0; }
            }
            const char* utf = glyph_to_utf8(g);
            if (utf) buf_puts(utf);
            else     buf_printf("%c", g);
        }
        reset_sgr();
    }
}

static void render_map(const sim_t* sim, const tui_state_t* st) {
    int rows = sim_rows(sim);
    int cols = sim_cols(sim);
    if (rows <= 0) rows = 1;
    if (cols <= 0) cols = 1;

    map_t m; map_init(&m, rows, cols);

    sim_node_view_t     nodes[TUI_MAX_NODES];
    sim_road_view_t     roads[TUI_MAX_ROADS];
    sim_station_view_t  stations[TUI_MAX_STATIONS];
    sim_unit_view_t     units[TUI_MAX_UNITS];
    sim_incident_view_t incidents[TUI_MAX_INCIDENTS];

    size_t nn = sim_nodes(sim, nodes, TUI_MAX_NODES);
    size_t nr = sim_roads(sim, roads, TUI_MAX_ROADS);
    size_t ns = sim_stations(sim, stations, TUI_MAX_STATIONS);
    size_t nu = sim_units(sim, units, TUI_MAX_UNITS);
    size_t ni = sim_incidents(sim, incidents, TUI_MAX_INCIDENTS);

    /* Build node-id -> row/col lookup. */
    int row_of[TUI_MAX_NODES], col_of[TUI_MAX_NODES];
    for (size_t i = 0; i < TUI_MAX_NODES; ++i) { row_of[i] = -1; col_of[i] = -1; }
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    for (size_t i = 0; i < nn; ++i) {
        int id = nodes[i].id;
        if (id >= 0 && id < TUI_MAX_NODES) {
            row_of[id] = nodes[i].row;
            col_of[id] = nodes[i].col;
        }
        if (nodes[i].x < min_x) min_x = nodes[i].x;
        if (nodes[i].y < min_y) min_y = nodes[i].y;
        if (nodes[i].x > max_x) max_x = nodes[i].x;
        if (nodes[i].y > max_y) max_y = nodes[i].y;
    }
    double span_x = (max_x > min_x) ? (max_x - min_x) : 1.0;
    double span_y = (max_y > min_y) ? (max_y - min_y) : 1.0;

    /* Roads first. */
    for (size_t i = 0; i < nr; ++i) {
        int a = roads[i].from_node, b = roads[i].to_node;
        if (a < 0 || a >= TUI_MAX_NODES || b < 0 || b >= TUI_MAX_NODES) continue;
        if (row_of[a] < 0 || row_of[b] < 0) continue;
        draw_road(&m, row_of[a], col_of[a], row_of[b], col_of[b]);
        if (roads[i].blocked) {
            int sx, sy;
            map_node_to_screen(&m,
                (row_of[a] + row_of[b]) / 2,
                (col_of[a] + col_of[b]) / 2,
                &sx, &sy);
            map_set(&m, sx, sy, 'X', 200, 60, 60, 0);
        }
    }

    /* Intersections. */
    for (size_t i = 0; i < nn; ++i) {
        int sx, sy;
        map_node_to_screen(&m, nodes[i].row, nodes[i].col, &sx, &sy);
        int flash = (st->flash_node == nodes[i].id);
        if (flash) map_set(&m, sx, sy, '.', 255, 255, 140, 1);
        else       map_set(&m, sx, sy, '.', 140, 145, 160, 0);
    }

    /* Stations. */
    for (size_t i = 0; i < ns; ++i) {
        int sx, sy;
        if (stations[i].node >= 0) {
            int id = stations[i].node;
            if (id < TUI_MAX_NODES && row_of[id] >= 0) {
                map_node_to_screen(&m, row_of[id], col_of[id], &sx, &sy);
            } else {
                map_world_to_screen(&m, stations[i].x, stations[i].y,
                                    span_x, span_y, min_x, min_y, &sx, &sy);
            }
        } else {
            map_world_to_screen(&m, stations[i].x, stations[i].y,
                                span_x, span_y, min_x, min_y, &sx, &sy);
        }
        char g = 'H'; int r = 240, gg = 90, b = 100;
        if (stations[i].type == SIM_UNIT_FIRE)   { g = 'F'; r = 245; gg = 160; b = 70; }
        if (stations[i].type == SIM_UNIT_POLICE) { g = 'P'; r = 100; gg = 150; b = 240; }
        map_set(&m, sx, sy, g, r, gg, b, 0);
    }

    /* Units. */
    for (size_t i = 0; i < nu; ++i) {
        int sx, sy;
        map_world_to_screen(&m, units[i].x, units[i].y,
                            span_x, span_y, min_x, min_y, &sx, &sy);
        char g = 'A'; int r = 240, gg = 90, b = 100;
        if (units[i].type == SIM_UNIT_FIRE)   { g = 'F'; r = 245; gg = 160; b = 70; }
        if (units[i].type == SIM_UNIT_POLICE) { g = 'P'; r = 100; gg = 150; b = 240; }
        if (units[i].state == SIM_UNIT_ENROUTE) { r = 120; gg = 200; b = 255; }
        map_set(&m, sx, sy, g, r, gg, b, 0);
    }

    /* Incidents. */
    for (size_t i = 0; i < ni; ++i) {
        if (incidents[i].resolved) continue;
        int sx, sy;
        int id = incidents[i].node;
        if (id >= 0 && id < TUI_MAX_NODES && row_of[id] >= 0) {
            map_node_to_screen(&m, row_of[id], col_of[id], &sx, &sy);
        } else {
            map_world_to_screen(&m, incidents[i].x, incidents[i].y,
                                span_x, span_y, min_x, min_y, &sx, &sy);
        }
        int r = 240, gg = 220, b = 90, blink = 0;
        if (incidents[i].severity == SIM_SEV_MED)  { r = 245; gg = 160; b = 70; }
        if (incidents[i].severity == SIM_SEV_HIGH) { r = 245; gg = 80;  b = 80; blink = 1; }
        map_set(&m, sx, sy, '!', r, gg, b, blink);
    }

    emit_map(&m);
}

static struct {
    size_t prev_pq;
    size_t prev_dispatch;
    size_t prev_total;
    size_t prev_resolved;
    double prev_time;
    size_t d_pq;
    size_t d_dispatch;
    size_t d_total;
    size_t d_resolved;
    int    initialised;
} g_act = { 0 };

static void render_stats(const sim_t* sim) {
    sim_metrics_t mt; sim_metrics(sim, &mt);

    if (g_act.initialised) {
        g_act.d_pq       = (mt.pq_operations  > g_act.prev_pq)       ? mt.pq_operations  - g_act.prev_pq       : 0;
        g_act.d_dispatch = (mt.dispatch_calls > g_act.prev_dispatch) ? mt.dispatch_calls - g_act.prev_dispatch : 0;
        g_act.d_total    = (mt.total_incidents > g_act.prev_total)   ? mt.total_incidents - g_act.prev_total   : 0;
        g_act.d_resolved = (mt.resolved_incidents > g_act.prev_resolved)
                           ? mt.resolved_incidents - g_act.prev_resolved : 0;
    }
    g_act.prev_pq        = mt.pq_operations;
    g_act.prev_dispatch  = mt.dispatch_calls;
    g_act.prev_total     = mt.total_incidents;
    g_act.prev_resolved  = mt.resolved_incidents;
    g_act.prev_time      = mt.sim_time;
    g_act.initialised = 1;

    int r = 2;
    fg(220, 222, 230);
    move_to(r++, STATS_COL0); buf_printf(" t    : %7.1fs       ", mt.sim_time);
    move_to(r++, STATS_COL0); buf_printf(" inc  : %3zu / %3zu res   ",
                                         mt.total_incidents, mt.resolved_incidents);
    move_to(r++, STATS_COL0); buf_printf(" pend : %3zu             ", mt.pending_incidents);
    move_to(r++, STATS_COL0); buf_printf(" units: %3zu (%zu idle)   ",
                                         mt.active_units, mt.idle_units);
    move_to(r++, STATS_COL0); buf_printf(" avg resp: %5.2fs        ", mt.avg_response_time);
    move_to(r++, STATS_COL0);
    fg(140, 145, 160);
    for (int i = 0; i < STATS_W - 2; ++i) buf_puts("\xe2\x94\x80");
    fg(160, 220, 255);
    move_to(r++, STATS_COL0); buf_puts(" Data structures         ");
    fg(220, 222, 230);
    move_to(r++, STATS_COL0); buf_printf(" comp    : %4zu         ", mt.road_components);
    move_to(r++, STATS_COL0); buf_printf(" PQ ops  : %6zu       ", mt.pq_operations);
    move_to(r++, STATS_COL0); buf_printf(" Huffman : %5.2f        ", mt.huffman_ratio);
    move_to(r++, STATS_COL0); buf_printf(" SA sfx  : %6zu       ", mt.sa_suffix_count);
    move_to(r++, STATS_COL0); buf_printf(" Dispatch: %6zu       ", mt.dispatch_calls);
    move_to(r++, STATS_COL0); buf_printf(" log B   : %6zu       ", mt.log_bytes);
    move_to(r++, STATS_COL0); buf_printf(" log B(H): %6zu       ", mt.log_bytes_huffman);

    move_to(r++, STATS_COL0);
    fg(140, 145, 160);
    for (int i = 0; i < STATS_W - 2; ++i) buf_puts("\xe2\x94\x80");
    fg(160, 220, 255);
    move_to(r++, STATS_COL0); buf_puts(" Activity (last frame)   ");
    if (g_act.d_dispatch > 0) fg(120, 230, 140);
    else                      fg(140, 145, 160);
    move_to(r++, STATS_COL0); buf_printf(" + dispatch : %3zu       ", g_act.d_dispatch);
    if (g_act.d_pq > 0) fg(120, 200, 255);
    else                fg(140, 145, 160);
    move_to(r++, STATS_COL0); buf_printf(" + PQ ops   : %3zu       ", g_act.d_pq);
    if (g_act.d_total > 0) fg(245, 160, 70);
    else                   fg(140, 145, 160);
    move_to(r++, STATS_COL0); buf_printf(" + spawned  : %3zu       ", g_act.d_total);
    if (g_act.d_resolved > 0) fg(120, 230, 140);
    else                      fg(140, 145, 160);
    move_to(r++, STATS_COL0); buf_printf(" + resolved : %3zu       ", g_act.d_resolved);
    reset_sgr();
}

static void clear_row(int row, int col0, int col1) {
    move_to(row, col0);
    for (int i = col0; i < col1; ++i) buf_puts(" ");
}

static void render_radio(const sim_t* sim) {
    char log[TUI_LOG_CAP];
    size_t n = sim_log_tail(sim, log, TUI_LOG_CAP);
    (void)n;
    /* Split into lines; show last 3. */
    const char* lines[32];
    int line_count = 0;
    const char* p = log;
    lines[line_count++] = p;
    while (*p && line_count < 32) {
        if (*p == '\n') {
            *(char*)p = '\0';
            lines[line_count++] = p + 1;
        }
        ++p;
    }
    int start = (line_count > 3) ? line_count - 3 : 0;
    int shown = 0;
    fg(220, 222, 230);
    for (int i = start; i < line_count && shown < 3; ++i) {
        if (lines[i][0] == '\0') continue;
        clear_row(MAP_INNER_H + 3 + shown, 2, TOTAL_W);
        move_to(MAP_INNER_H + 3 + shown, 3);
        fg(160, 220, 255); buf_puts("Radio ");
        fg(220, 222, 230);
        int maxlen = TOTAL_W - 10;
        int len = (int)strlen(lines[i]);
        if (len > maxlen) len = maxlen;
        for (int k = 0; k < len; ++k) {
            char c = lines[i][k];
            if (c == '\r') continue;
            buf_printf("%c", c);
        }
        ++shown;
    }
    while (shown < 3) {
        clear_row(MAP_INNER_H + 3 + shown, 2, TOTAL_W);
        ++shown;
    }
    reset_sgr();
}

static void render_search(const tui_state_t* st) {
    clear_row(MAP_INNER_H + 7, 2, TOTAL_W);
    move_to(MAP_INNER_H + 7, 3);
    fg(160, 220, 255);
    buf_puts(st->search_mode == TUI_SEARCH_FUZZY ? "Fuzzy:  " : "Search: ");
    fg(220, 222, 230);
    if (st->search_len == 0) {
        fg(140, 145, 160);
        buf_puts("_");
    } else {
        buf_printf("%s", st->search);
    }
    /* Completions. */
    clear_row(MAP_INNER_H + 8, 2, TOTAL_W);
    move_to(MAP_INNER_H + 8, 3);
    fg(140, 145, 160);
    buf_puts(" > ");
    for (size_t i = 0; i < st->n_completions && i < 5; ++i) {
        if ((int)i == st->selected_completion) fg(160, 220, 255);
        else                                   fg(140, 145, 160);
        buf_printf("%s", st->completions[i]);
        fg(140, 145, 160);
        if (i + 1 < st->n_completions && i + 1 < 5) buf_puts("  |  ");
    }
    reset_sgr();
}

static void render_hint(const sim_t* sim, double spawn_rate) {
    move_to(HINT_ROW + 1, 1);
    clear_row(HINT_ROW + 1, 1, TOTAL_W);
    move_to(HINT_ROW + 1, 1);
    fg(140, 145, 160);
    buf_puts(" Map: ");
    fg(240, 90, 100);  buf_puts("H/A");
    fg(140, 145, 160); buf_puts(" hosp+amb  ");
    fg(245, 160, 70);  buf_puts("F");
    fg(140, 145, 160); buf_puts(" fire  ");
    fg(100, 150, 240); buf_puts("P");
    fg(140, 145, 160); buf_puts(" police  ");
    fg(245, 80, 80);   buf_puts("!");
    fg(140, 145, 160); buf_printf(" incident   |  [SPACE] %s  [S] spawn  [+/-] %.1f/s  [TAB] %s  [?] help  [Q] quit",
                                 sim_is_paused(sim) ? "resume" : "pause",
                                 spawn_rate,
                                 "fuzzy");
    reset_sgr();
}

static void render_help_overlay(void) {
    static const char* lines[] = {
        " GridWatch — Emergency Dispatch Simulator",
        "",
        " You're watching live dispatch on a grid city.",
        " Each metric on the right is a real DS counter:",
        "",
        "  Fibonacci heap  -> Dijkstra PQ for routing",
        "  Quadtree        -> nearest-idle-unit lookup",
        "  DEPQ            -> severity-ordered pending pool",
        "  AVL / RB / B+   -> unit + incident indexes",
        "  Trie + BK-tree  -> street autocomplete + fuzzy",
        "  Suffix array    -> radio-log substring search",
        "  Huffman         -> live log compression ratio",
        "  DSU             -> connected road components",
        "  Persistent list -> immutable event history",
        "",
        " Glyphs:",
        "   H/A  hospital + ambulance     ! incident",
        "   F    fire station + truck     . intersection",
        "   P    police + cruiser         X blocked road",
        "",
        " Controls:",
        "   SPACE  pause / resume    S  force-spawn",
        "   +/-    spawn rate        TAB toggle fuzzy",
        "   Q/ESC  quit              ?  toggle this help",
        "",
        " Full walkthrough: docs/CONCEPTS.md",
        "",
        " (press ? or ESC to dismiss)",
        NULL
    };
    fg(220, 222, 230);
    int y = MAP_ROW0 + 2;
    for (int i = 0; lines[i] != NULL; ++i) {
        if (y > MAP_INNER_H) break;
        move_to(y, MAP_COL0 + 2);
        for (int j = 0; j < MAP_INNER_W - 2; ++j) buf_puts(" ");
        move_to(y, MAP_COL0 + 2);
        if (i == 0) fg(160, 220, 255);
        else        fg(220, 222, 230);
        buf_puts(lines[i]);
        ++y;
    }
    reset_sgr();
}

void tui_render_frame(const sim_t* sim, const tui_state_t* st, double spawn_rate) {
    buf_reset();
    buf_puts("\x1b[H");
    draw_box_borders();
    if (st->help_visible) {
        render_stats(sim);
        render_radio(sim);
        render_search(st);
        render_hint(sim, spawn_rate);
        render_help_overlay();
    } else {
        render_map(sim, st);
        render_stats(sim);
        render_radio(sim);
        render_search(st);
        render_hint(sim, spawn_rate);
    }
    move_to(HINT_ROW + 2, 1);
    fwrite(g_buf, 1, g_len, stdout);
    fflush(stdout);
}
