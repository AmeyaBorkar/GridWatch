#include "tui.h"
#include "dispatch/sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>

#define TARGET_FPS 30
#define TARGET_MS  (1000 / TARGET_FPS)

static sim_t*      g_sim = NULL;
static tui_state_t g_state;
static double      g_spawn_rate = 0.4;

static double now_seconds(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static void on_exit_cleanup(void) {
    if (g_sim) { sim_destroy(g_sim); g_sim = NULL; }
    tui_state_reset_completions(&g_state);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    int   rows = 10, cols = 12;
    uint64_t seed = 0xD15DA7CDULL;

    g_sim = sim_create(rows, cols, seed);
    if (!g_sim) {
        fprintf(stderr, "sim_create failed\n");
        return 1;
    }
    sim_set_spawn_rate(g_sim, g_spawn_rate);

    tui_state_init(&g_state);
    g_state.last_spawn_rate = g_spawn_rate;

    atexit(on_exit_cleanup);
    tui_console_init();

    double prev = now_seconds();
    while (!g_state.quit) {
        double t = now_seconds();
        double dt = t - prev;
        if (dt > 0.25) dt = 0.25;
        prev = t;

        tui_handle_input(g_sim, &g_state, &g_spawn_rate);
        if (g_state.quit) break;

        sim_tick(g_sim, dt);

        if (g_state.flash_node >= 0) {
            sim_metrics_t m; sim_metrics(g_sim, &m);
            if (m.sim_time >= g_state.flash_until) g_state.flash_node = -1;
        }

        tui_render_frame(g_sim, &g_state, g_spawn_rate);

        double spent = now_seconds() - t;
        int sleep_ms = (int)((1.0 / TARGET_FPS - spent) * 1000.0);
        if (sleep_ms > 0) Sleep((DWORD)sleep_ms);
    }

    return 0;
}
