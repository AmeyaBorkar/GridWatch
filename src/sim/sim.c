#include "sim_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

extern void sim_rebuild_idle_quad(struct sim* S);
extern void sim_dispatch_pending(struct sim* S);

/* splitmix64 for local deterministic rng */
uint64_t sim_splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double sim_rand01(struct sim* S) {
    uint64_t r = sim_splitmix64(&S->prng_state);
    return (double)(r >> 11) * (1.0 / (double)(1ULL << 53));
}

int sim_rand_int(struct sim* S, int lo, int hi) {
    if (hi < lo) return lo;
    uint64_t r = sim_splitmix64(&S->prng_state);
    return lo + (int)(r % (uint64_t)(hi - lo + 1));
}

/* ---------- lifecycle ---------- */
sim_t* sim_create(int rows, int cols, uint64_t seed) {
    if (rows < 2 || cols < 2) return NULL;
    sim_t* S = (sim_t*)calloc(1, sizeof(sim_t));
    if (!S) return NULL;
    S->rows = rows; S->cols = cols;
    S->seed = seed;
    S->prng_state = seed ? seed : 0xA5A5A5A5A5A5A5A5ULL;
    S->paused = 0;
    S->spawn_rate = 0.4;
    S->spawn_accum = 0.0;
    S->sim_time = 0.0;
    S->next_unit_id = 0;
    S->next_incident_id = 0;

    rnd_seed(seed);

    /* build city/roads */
    if (sim_build_city(S) != 0) { sim_destroy(S); return NULL; }

    /* log */
    sim_log_init(&S->log);

    /* trees */
    S->unit_by_id       = tree_avl_create();
    S->pending_by_spawn = tree_rb_create();
    S->recent_units     = tree_splay_create();
    S->incidents_log    = tree_bplus_create(4);
    S->pending_alt      = tree_threaded_create();
    if (!S->unit_by_id || !S->pending_by_spawn || !S->recent_units ||
        !S->incidents_log || !S->pending_alt) {
        sim_destroy(S); return NULL;
    }

    /* heaps */
    S->h_binom   = heap_binom_create();
    S->h_leftist = heap_leftist_create();
    S->h_skew    = heap_skew_create();
    S->h_pairing = heap_pairing_create();
    S->pending_depq = depq_create();
    if (!S->h_binom || !S->h_leftist || !S->h_skew || !S->h_pairing || !S->pending_depq) {
        sim_destroy(S); return NULL;
    }
    /* warm-up: exercise one push/pop each */
    heap_binom_push(S->h_binom, 1.0, NULL);
    heap_leftist_push(S->h_leftist, 1.0, NULL);
    heap_skew_push(S->h_skew, 1.0, NULL);
    heap_pairing_push(S->h_pairing, 1.0, NULL);
    { ds_entry_t e;
      heap_binom_pop_min(S->h_binom, &e);
      heap_leftist_pop_min(S->h_leftist, &e);
      heap_skew_pop_min(S->h_skew, &e);
      heap_pairing_pop_min(S->h_pairing, &e);
    }

    /* randomized */
    S->eta_board    = rnd_skip_create();
    S->station_load = rnd_treap_create();

    /* spatial */
    double w = (double)cols * 64.0 + 64.0;
    double h = (double)rows * 64.0 + 64.0;
    S->q_idle         = sp_quad_create(0.0, 0.0, w, h);
    S->rt_stations    = sp_rtree_create();
    S->seg_incidents60 = sp_seg_create(60);
    S->itree_shifts   = sp_itree_create();
    if (!S->eta_board || !S->station_load || !S->q_idle || !S->rt_stations ||
        !S->seg_incidents60 || !S->itree_shifts) {
        sim_destroy(S); return NULL;
    }

    /* misc */
    S->plist = misc_plist_create();
    S->plist_head = NULL;
    S->bv_has_incident = NULL;

    /* search module */
    if (sim_search_init(S) != 0) { sim_destroy(S); return NULL; }

    /* stations + units */
    if (sim_init_stations_units(S) != 0) { sim_destroy(S); return NULL; }

    /* Build KD tree from initial node points (rebuilt infrequently). */
    {
        sp_point_t* pts = (sp_point_t*)malloc(sizeof(sp_point_t) * S->n_nodes);
        if (pts) {
            for (size_t i = 0; i < S->n_nodes; i++) {
                pts[i].x = S->nodes[i].x;
                pts[i].y = S->nodes[i].y;
                pts[i].data = (ds_val_t)(intptr_t)i;
            }
            S->kd_units   = sp_kd_build(pts, S->n_nodes);
            S->range_units = sp_range_build(pts, S->n_nodes);
            /* BSP partition from station points */
            sp_point_t sp[3];
            for (size_t i = 0; i < S->n_stations; i++) {
                sp[i].x = S->stations[i].x;
                sp[i].y = S->stations[i].y;
                sp[i].data = (ds_val_t)(intptr_t)S->stations[i].id;
            }
            S->bsp_stations = sp_bsp_build(sp, S->n_stations);
            free(pts);
        }
    }

    sim_rebuild_idle_quad(S);
    sim_log_append(S, "[t=%.1f] dispatch ready: %dx%d grid, %zu stations\n",
                   S->sim_time, rows, cols, S->n_stations);
    return S;
}

void sim_destroy(sim_t* S) {
    if (!S) return;

    sim_search_free(S);
    sim_free_entities(S);
    sim_free_city(S);
    sim_log_free(&S->log);

    if (S->unit_by_id)       tree_avl_destroy(S->unit_by_id);
    if (S->pending_by_spawn) tree_rb_destroy(S->pending_by_spawn);
    if (S->recent_units)     tree_splay_destroy(S->recent_units);
    if (S->incidents_log)    tree_bplus_destroy(S->incidents_log);
    if (S->pending_alt)      tree_threaded_destroy(S->pending_alt);

    if (S->h_binom)   heap_binom_destroy(S->h_binom);
    if (S->h_leftist) heap_leftist_destroy(S->h_leftist);
    if (S->h_skew)    heap_skew_destroy(S->h_skew);
    if (S->h_pairing) heap_pairing_destroy(S->h_pairing);
    if (S->pending_depq) depq_destroy(S->pending_depq);

    if (S->eta_board)    rnd_skip_destroy(S->eta_board);
    if (S->station_load) rnd_treap_destroy(S->station_load);

    if (S->q_idle)         sp_quad_destroy(S->q_idle);
    if (S->kd_units)       sp_kd_destroy(S->kd_units);
    if (S->rt_stations)    sp_rtree_destroy(S->rt_stations);
    if (S->seg_incidents60) sp_seg_destroy(S->seg_incidents60);
    if (S->itree_shifts)   sp_itree_destroy(S->itree_shifts);
    if (S->range_units)    sp_range_destroy(S->range_units);
    if (S->bsp_stations)   sp_bsp_destroy(S->bsp_stations);

    if (S->plist)          misc_plist_destroy(S->plist);
    if (S->bv_has_incident) misc_bv_destroy(S->bv_has_incident);

    free(S);
}

void sim_set_paused(sim_t* S, int paused) { if (S) S->paused = paused ? 1 : 0; }
int  sim_is_paused(const sim_t* S) { return S ? S->paused : 0; }
void sim_set_spawn_rate(sim_t* S, double per_second) {
    if (S && per_second >= 0.0) S->spawn_rate = per_second;
}

int sim_force_spawn(sim_t* S) {
    if (!S) return -1;
    sim_incident_t* inc = sim_spawn_incident(S, 1);
    return inc ? inc->id : -1;
}

void sim_tick(sim_t* S, double dt) {
    if (!S || S->paused || dt <= 0.0) return;

    S->sim_time += dt;

    /* 1) dispatch pending first */
    sim_dispatch_pending(S);

    /* 2) advance units */
    sim_tick_units(S, dt);

    /* 3) spawn new incidents */
    S->spawn_accum += dt * S->spawn_rate;
    while (S->spawn_accum >= 1.0) {
        sim_spawn_incident(S, 0);
        S->spawn_accum -= 1.0;
    }

    /* 4) rebuild idle quad + metrics */
    sim_rebuild_idle_quad(S);
    sim_metrics_tick(S, dt);
}

/* ---------- accessors ---------- */
int sim_rows(const sim_t* S) { return S ? S->rows : 0; }
int sim_cols(const sim_t* S) { return S ? S->cols : 0; }

size_t sim_nodes(const sim_t* S, sim_node_view_t* out, size_t max) {
    if (!S || !out) return 0;
    size_t n = S->n_nodes < max ? S->n_nodes : max;
    for (size_t i = 0; i < n; i++) {
        const sim_node_t* nd = &S->nodes[i];
        sim_node_view_t* v = &out[i];
        v->id = nd->id;
        v->row = nd->row;
        v->col = nd->col;
        v->x = nd->x;
        v->y = nd->y;
        memcpy(v->street, nd->street, sizeof(v->street));
    }
    return n;
}

size_t sim_roads(const sim_t* S, sim_road_view_t* out, size_t max) {
    if (!S || !out) return 0;
    size_t n = S->n_roads < max ? S->n_roads : max;
    for (size_t i = 0; i < n; i++) {
        const sim_road_t* rd = &S->roads[i];
        sim_road_view_t* v = &out[i];
        v->id = rd->id;
        v->from_node = rd->from_node;
        v->to_node = rd->to_node;
        v->length = rd->length;
        v->blocked = rd->blocked;
    }
    return n;
}

size_t sim_stations(const sim_t* S, sim_station_view_t* out, size_t max) {
    if (!S || !out) return 0;
    size_t n = S->n_stations < max ? S->n_stations : max;
    for (size_t i = 0; i < n; i++) {
        const sim_station_t* st = &S->stations[i];
        sim_station_view_t* v = &out[i];
        v->id = st->id;
        v->type = st->type;
        v->node = st->node;
        v->x = st->x;
        v->y = st->y;
        v->n_units = st->n_units;
        memcpy(v->name, st->name, sizeof(v->name));
    }
    return n;
}

size_t sim_units(const sim_t* S, sim_unit_view_t* out, size_t max) {
    if (!S || !out) return 0;
    size_t n = S->n_units < max ? S->n_units : max;
    for (size_t i = 0; i < n; i++) {
        const sim_unit_t* u = &S->units[i];
        sim_unit_view_t* v = &out[i];
        v->id = u->id;
        v->station_id = u->station_id;
        v->type = u->type;
        v->state = u->state;
        v->cur_node = u->cur_node;
        v->target_node = u->target_node;
        v->x = u->x;
        v->y = u->y;
        v->incident_id = u->incident_id;
        v->eta = u->eta;
    }
    return n;
}

size_t sim_incidents(const sim_t* S, sim_incident_view_t* out, size_t max) {
    if (!S || !out) return 0;
    size_t n = S->n_incidents < max ? S->n_incidents : max;
    for (size_t i = 0; i < n; i++) {
        const sim_incident_t* inc = &S->incidents[i];
        sim_incident_view_t* v = &out[i];
        v->id = inc->id;
        v->type = inc->type;
        v->severity = inc->severity;
        v->node = inc->node;
        v->x = inc->x;
        v->y = inc->y;
        v->spawn_time = inc->spawn_time;
        v->assigned_unit = inc->assigned_unit;
        v->resolved = inc->resolved;
        v->response_time = inc->response_time;
    }
    return n;
}

/* ---------- search ---------- */
size_t sim_autocomplete(const sim_t* S, const char* prefix, char** out, size_t max) {
    if (!S || !S->trie || !prefix || !out || max == 0) return 0;
    return str_trie_prefix(S->trie, prefix, out, max);
}

size_t sim_fuzzy(const sim_t* S, const char* q, int max_edits, char** out, size_t max) {
    if (!S || !S->fuzzy || !q || !out || max == 0) return 0;
    return str_fuzzy_search(S->fuzzy, q, max_edits, out, max);
}

int sim_node_by_street(const sim_t* S, const char* name) {
    if (!S || !name) return -1;
    for (size_t i = 0; i < S->n_nodes; i++) {
        if (strcmp(S->nodes[i].street, name) == 0) return S->nodes[i].id;
    }
    return -1;
}

/* ---------- metrics + log ---------- */
void sim_metrics(const sim_t* S, sim_metrics_t* out) {
    if (!S || !out) return;
    memset(out, 0, sizeof(*out));
    out->sim_time = S->sim_time;
    out->total_incidents = S->n_incidents;
    out->resolved_incidents = S->resolved_count;

    size_t pending = 0;
    size_t idle = 0, active = 0;
    for (size_t i = 0; i < S->n_incidents; i++) {
        if (!S->incidents[i].resolved) pending++;
    }
    for (size_t i = 0; i < S->n_units; i++) {
        if (S->units[i].state == SIM_UNIT_IDLE) idle++;
        else active++;
    }
    out->pending_incidents = pending;
    out->idle_units = idle;
    out->active_units = active;
    out->avg_response_time = (S->resolved_count > 0)
        ? (S->total_response_time / (double)S->resolved_count) : 0.0;
    out->road_components = S->road_components;

    out->log_bytes = S->log.len;
    if (S->log.len > 0) {
        /* Build a fresh huffman for accurate current ratio. */
        huffman_t* h = huffman_build((const uint8_t*)S->log.buf, S->log.len);
        if (h) {
            double r = huffman_ratio(h, (const uint8_t*)S->log.buf, S->log.len);
            out->huffman_ratio = r;
            out->log_bytes_huffman = (size_t)(r * (double)S->log.len);
            huffman_destroy(h);
        } else {
            out->huffman_ratio = 1.0;
            out->log_bytes_huffman = S->log.len;
        }
    } else {
        out->huffman_ratio = 0.0;
        out->log_bytes_huffman = 0;
    }
    out->sa_suffix_count = (S->log.sa != NULL) ? S->log.len : 0;
    out->dispatch_calls = S->dispatch_calls;
    out->pq_operations = S->pq_operations;
}

size_t sim_log_tail(const sim_t* S, char* buf, size_t cap) {
    if (!S || !buf || cap == 0) return 0;
    size_t copy = S->log.len < (cap - 1) ? S->log.len : (cap - 1);
    size_t start = S->log.len - copy;
    memcpy(buf, S->log.buf + start, copy);
    buf[copy] = '\0';
    return copy;
}

size_t sim_log_count(const sim_t* S, const char* pattern) {
    if (!S || !pattern) return 0;
    size_t m = strlen(pattern);
    if (m == 0 || S->log.len == 0) return 0;
    if (S->log.sa) return str_sa_count(S->log.sa, pattern, m);
    /* fallback: naive scan */
    size_t count = 0;
    for (size_t i = 0; i + m <= S->log.len; i++) {
        if (memcmp(S->log.buf + i, pattern, m) == 0) count++;
    }
    return count;
}
