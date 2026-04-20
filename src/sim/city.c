#include "sim_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char* const SIM_STREETS[SIM_STREET_POOL_SZ] = {
    "Abbey", "Birch", "Cedar", "Dover", "Elm", "Forest", "Grove", "Harbor",
    "Ivy", "Jasmine", "Kennedy", "Laurel", "Maple", "Nova", "Oak", "Pearl",
    "Quartz", "Rose", "Sunset", "Thorn", "Union", "Vine", "Willow", "Yew"
};

int sim_node_index(const struct sim* S, int r, int c) {
    if (r < 0 || c < 0 || r >= S->rows || c >= S->cols) return -1;
    return r * S->cols + c;
}

static void make_street_name(int r, int c, char* out, size_t cap) {
    const char* row_s = SIM_STREETS[r % SIM_STREET_POOL_SZ];
    const char* col_s = SIM_STREETS[c % SIM_STREET_POOL_SZ];
    snprintf(out, cap, "%s & %s", row_s, col_s);
}

int sim_build_city(struct sim* S) {
    int R = S->rows, C = S->cols;
    S->n_nodes = (size_t)R * (size_t)C;
    S->nodes = (sim_node_t*)calloc(S->n_nodes, sizeof(sim_node_t));
    if (!S->nodes) return -1;

    for (int r = 0; r < R; r++) {
        for (int c = 0; c < C; c++) {
            int id = r * C + c;
            sim_node_t* n = &S->nodes[id];
            n->id = id;
            n->row = r;
            n->col = c;
            n->x = (double)c * 64.0 + 32.0;
            n->y = (double)r * 64.0 + 32.0;
            make_street_name(r, c, n->street, sizeof(n->street));
        }
    }

    /* build roads: horizontal + vertical edges, every 7th blocked */
    size_t max_roads = (size_t)(R * (C - 1)) + (size_t)((R - 1) * C);
    S->roads = (sim_road_t*)calloc(max_roads ? max_roads : 1, sizeof(sim_road_t));
    if (!S->roads) return -1;
    S->n_roads = 0;

    int counter = 0;
    for (int r = 0; r < R; r++) {
        for (int c = 0; c < C - 1; c++) {
            sim_road_t* rd = &S->roads[S->n_roads];
            rd->id = (int)S->n_roads;
            rd->from_node = r * C + c;
            rd->to_node   = r * C + (c + 1);
            rd->length    = 64.0;
            rd->blocked   = (counter % 7 == 6) ? 1 : 0;
            counter++;
            S->n_roads++;
        }
    }
    for (int r = 0; r < R - 1; r++) {
        for (int c = 0; c < C; c++) {
            sim_road_t* rd = &S->roads[S->n_roads];
            rd->id = (int)S->n_roads;
            rd->from_node = r * C + c;
            rd->to_node   = (r + 1) * C + c;
            rd->length    = 64.0;
            rd->blocked   = (counter % 7 == 6) ? 1 : 0;
            counter++;
            S->n_roads++;
        }
    }

    /* build CSR */
    S->adj_head = (int*)calloc(S->n_nodes + 1, sizeof(int));
    if (!S->adj_head) return -1;
    size_t e2 = S->n_roads * 2;
    S->adj_to      = (int*)malloc(sizeof(int) * (e2 ? e2 : 1));
    S->adj_w       = (double*)malloc(sizeof(double) * (e2 ? e2 : 1));
    S->adj_blocked = (int*)malloc(sizeof(int) * (e2 ? e2 : 1));
    if (!S->adj_to || !S->adj_w || !S->adj_blocked) return -1;

    for (size_t i = 0; i < S->n_roads; i++) {
        S->adj_head[S->roads[i].from_node + 1]++;
        S->adj_head[S->roads[i].to_node + 1]++;
    }
    for (size_t i = 1; i <= S->n_nodes; i++) S->adj_head[i] += S->adj_head[i - 1];
    int* cursor = (int*)calloc(S->n_nodes, sizeof(int));
    if (!cursor) return -1;
    for (size_t i = 0; i < S->n_roads; i++) {
        sim_road_t* rd = &S->roads[i];
        int a = rd->from_node, b = rd->to_node;
        int pa = S->adj_head[a] + cursor[a]++;
        int pb = S->adj_head[b] + cursor[b]++;
        S->adj_to[pa] = b; S->adj_w[pa] = rd->length; S->adj_blocked[pa] = rd->blocked;
        S->adj_to[pb] = a; S->adj_w[pb] = rd->length; S->adj_blocked[pb] = rd->blocked;
    }
    free(cursor);

    /* DSU over non-blocked roads */
    S->dsu = misc_dsu_create(S->n_nodes);
    if (!S->dsu) return -1;
    for (size_t i = 0; i < S->n_roads; i++) {
        if (!S->roads[i].blocked) {
            misc_dsu_union(S->dsu, (size_t)S->roads[i].from_node,
                                   (size_t)S->roads[i].to_node);
        }
    }
    S->road_components = misc_dsu_count(S->dsu);

    /* find the largest connected component as "main" */
    size_t best_root = 0, best_sz = 0;
    for (size_t i = 0; i < S->n_nodes; i++) {
        size_t r = misc_dsu_find(S->dsu, i);
        size_t sz = misc_dsu_size(S->dsu, r);
        if (sz > best_sz) { best_sz = sz; best_root = r; }
    }
    S->main_cc_root = (int)best_root;
    for (size_t i = 0; i < S->n_nodes; i++) {
        S->nodes[i].in_main_cc = (misc_dsu_find(S->dsu, i) == best_root) ? 1 : 0;
    }

    return 0;
}

void sim_free_city(struct sim* S) {
    free(S->nodes); S->nodes = NULL;
    free(S->roads); S->roads = NULL;
    free(S->adj_head); S->adj_head = NULL;
    free(S->adj_to); S->adj_to = NULL;
    free(S->adj_w); S->adj_w = NULL;
    free(S->adj_blocked); S->adj_blocked = NULL;
    if (S->dsu) { misc_dsu_destroy(S->dsu); S->dsu = NULL; }
}
