/* test_sim.c — integration test for the simulation layer. */
#include "dispatch/sim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    sim_t* S = sim_create(8, 8, 42ULL);
    assert(S);
    assert(sim_rows(S) == 8);
    assert(sim_cols(S) == 8);

    /* force a few incidents up-front */
    for (int i = 0; i < 3; i++) {
        int id = sim_force_spawn(S);
        assert(id >= 0);
    }

    /* Run ~60s of sim at 0.1s dt (600 ticks). */
    sim_set_spawn_rate(S, 0.5);
    for (int i = 0; i < 600; i++) {
        sim_tick(S, 0.1);
    }

    sim_metrics_t m;
    sim_metrics(S, &m);
    printf("sim_time=%.2f incidents=%zu resolved=%zu pending=%zu\n",
           m.sim_time, m.total_incidents, m.resolved_incidents, m.pending_incidents);
    printf("idle=%zu active=%zu avg_rt=%.2f components=%zu\n",
           m.idle_units, m.active_units, m.avg_response_time, m.road_components);
    printf("log_bytes=%zu huff=%zu ratio=%.3f sa=%zu dispatch_calls=%zu pq_ops=%zu\n",
           m.log_bytes, m.log_bytes_huffman, m.huffman_ratio,
           m.sa_suffix_count, m.dispatch_calls, m.pq_operations);

    assert(m.total_incidents > 0);
    assert(m.dispatch_calls > 0);
    assert(m.road_components >= 1);
    assert(m.huffman_ratio >= 0.0 && m.huffman_ratio <= 1.0);

    /* autocomplete: "Abbey" is a street word in the pool; prefix should find it. */
    char* out[32] = {0};
    size_t ncomp = sim_autocomplete(S, "Abbey", out, 32);
    printf("autocomplete 'Abbey' -> %zu results\n", ncomp);
    int found_abbey = 0;
    for (size_t i = 0; i < ncomp; i++) {
        if (strcmp(out[i], "Abbey") == 0) found_abbey = 1;
        free(out[i]);
    }
    assert(found_abbey);

    /* a fully-qualified prefix yields exactly one completion. */
    char* one[4] = {0};
    size_t nex = sim_autocomplete(S, "Abbey & Harbor", one, 4);
    printf("autocomplete 'Abbey & Harbor' -> %zu\n", nex);
    assert(nex == 1);
    assert(strcmp(one[0], "Abbey & Harbor") == 0);
    free(one[0]);

    /* fuzzy */
    char* fout[16] = {0};
    size_t nf = sim_fuzzy(S, "Maplle", 2, fout, 16);
    printf("fuzzy 'Maplle' -> %zu\n", nf);
    for (size_t i = 0; i < nf; i++) free(fout[i]);

    /* nodes / roads / units snapshots */
    sim_node_view_t   nv[128];
    sim_road_view_t   rv[256];
    sim_unit_view_t   uv[64];
    sim_station_view_t sv[8];
    size_t nn = sim_nodes(S, nv, 128);
    size_t nr = sim_roads(S, rv, 256);
    size_t nu = sim_units(S, uv, 64);
    size_t ns = sim_stations(S, sv, 8);
    printf("snapshot nodes=%zu roads=%zu units=%zu stations=%zu\n", nn, nr, nu, ns);
    assert(nn > 0 && nr > 0 && nu > 0 && ns == 3);

    /* node lookup by street */
    int nid = sim_node_by_street(S, nv[0].street);
    assert(nid == nv[0].id);

    /* log tail */
    char tail[512];
    size_t t = sim_log_tail(S, tail, sizeof(tail));
    printf("log tail (%zu bytes):\n%.*s\n", t, (int)t, tail);

    /* log count for 'dispatched' */
    size_t dc = sim_log_count(S, "dispatched");
    printf("log count 'dispatched' -> %zu\n", dc);

    sim_destroy(S);
    printf("PASS\n");
    return 0;
}
