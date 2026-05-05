#include "sim_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char* unit_type_prefix(sim_unit_type_t t) {
    switch (t) {
        case SIM_UNIT_AMBULANCE: return "AMB";
        case SIM_UNIT_FIRE:      return "FIR";
        case SIM_UNIT_POLICE:    return "POL";
    }
    return "UNK";
}

static const char* inc_type_name(sim_inc_type_t t) {
    switch (t) {
        case SIM_INC_MEDICAL: return "medical";
        case SIM_INC_FIRE:    return "fire";
        case SIM_INC_CRIME:   return "crime";
    }
    return "unknown";
}

static sim_unit_type_t unit_for_incident(sim_inc_type_t t) {
    switch (t) {
        case SIM_INC_MEDICAL: return SIM_UNIT_AMBULANCE;
        case SIM_INC_FIRE:    return SIM_UNIT_FIRE;
        case SIM_INC_CRIME:   return SIM_UNIT_POLICE;
    }
    return SIM_UNIT_AMBULANCE;
}

static int random_main_cc_node(struct sim* S) {
    for (int tries = 0; tries < 64; tries++) {
        int idx = sim_rand_int(S, 0, (int)S->n_nodes - 1);
        if (S->nodes[idx].in_main_cc) return idx;
    }
    for (size_t i = 0; i < S->n_nodes; i++) {
        if (S->nodes[i].in_main_cc) return (int)i;
    }
    return 0;
}

/* UNIT CREATION: appends a new unit to the dynamic array (geometric growth)
 * and registers it in the secondary indices — AVL by id for O(log n) lookup,
 * QUADTREE for nearest-idle spatial search, INTERVAL TREE for shift coverage.
 * Each index lets a different query about units run efficiently. */
static int add_unit(struct sim* S, sim_station_t* st) {
    if (S->n_units == S->cap_units) {
        size_t nc = S->cap_units ? S->cap_units * 2 : 16;
        sim_unit_t* nu = (sim_unit_t*)realloc(S->units, nc * sizeof(sim_unit_t));
        if (!nu) return -1;
        S->units = nu;
        S->cap_units = nc;
    }
    sim_unit_t* u = &S->units[S->n_units++];
    memset(u, 0, sizeof(*u));
    u->id = S->next_unit_id++;
    u->station_id = st->id;
    u->type = st->type;
    u->state = SIM_UNIT_IDLE;
    u->cur_node = st->node;
    u->target_node = -1;
    u->x = st->x;
    u->y = st->y;
    u->incident_id = -1;
    u->shift_start = 0.0;
    u->shift_end   = 8.0 * 3600.0;
    tree_avl_insert(S->unit_by_id, (ds_key_t)u->id, u);
    sp_quad_insert(S->q_idle, u->x, u->y, (ds_val_t)(intptr_t)u->id);
    sp_itree_insert(S->itree_shifts, u->shift_start, u->shift_end, (ds_val_t)(intptr_t)u->id);
    return 0;
}

int sim_init_stations_units(struct sim* S) {
    S->n_stations = 3;
    S->stations = (sim_station_t*)calloc(3, sizeof(sim_station_t));
    if (!S->stations) return -1;

    const char* names[3] = { "Central Ambulance", "Ladder 1", "Precinct 7" };
    sim_unit_type_t types[3] = { SIM_UNIT_AMBULANCE, SIM_UNIT_FIRE, SIM_UNIT_POLICE };

    /* Reserve capacity for all spawnable pools. */
    S->cap_units = 32;
    S->units = (sim_unit_t*)calloc(S->cap_units, sizeof(sim_unit_t));
    if (!S->units) return -1;

    S->cap_incidents = 64;
    S->incidents = (sim_incident_t*)calloc(S->cap_incidents, sizeof(sim_incident_t));
    if (!S->incidents) return -1;

    int chosen[3] = { -1, -1, -1 };
    for (int i = 0; i < 3; i++) {
        int n;
        for (int tries = 0; tries < 128; tries++) {
            n = random_main_cc_node(S);
            int dup = 0;
            for (int j = 0; j < i; j++) if (chosen[j] == n) { dup = 1; break; }
            if (!dup) { chosen[i] = n; break; }
        }
        if (chosen[i] < 0) chosen[i] = random_main_cc_node(S);

        sim_station_t* st = &S->stations[i];
        st->id = i;
        st->type = types[i];
        st->node = chosen[i];
        st->x = S->nodes[chosen[i]].x;
        st->y = S->nodes[chosen[i]].y;
        snprintf(st->name, sizeof(st->name), "%s", names[i]);

        int count = 3 + sim_rand_int(S, 0, 2); /* 3..5 */
        st->n_units = count;
        for (int k = 0; k < count; k++) add_unit(S, st);

        /* station coverage rect (roughly +/- 200 units). */
        sp_rtree_insert(S->rt_stations,
                        st->x - 200.0, st->y - 200.0,
                        st->x + 200.0, st->y + 200.0,
                        (ds_val_t)(intptr_t)st->id);

        /* treap workload keyed by id */
        rnd_treap_insert(S->station_load, (ds_key_t)st->id, (ds_val_t)(intptr_t)0);
    }

    return 0;
}

/* UNIT/INCIDENT DESTRUCTION: tears down the underlying arrays. The secondary
 * index structures (AVL, RB, B+, etc.) own their own nodes and are freed
 * separately by the sim shutdown path. */
void sim_free_entities(struct sim* S) {
    free(S->stations); S->stations = NULL;
    free(S->units); S->units = NULL;
    free(S->incidents); S->incidents = NULL;
}

/* INCIDENT SPAWN + MULTI-INDEX FAN-OUT: creates one new incident record and
 * threads it into every relevant ADS — B+ TREE incident log (range scans),
 * RED-BLACK + THREADED BSTs keyed by spawn time (pending queue + replay),
 * a PRIORITY DEQUE by severity, the PERSISTENT LIST history, and a SEGMENT
 * TREE rolling counter. One write, many indices. */
sim_incident_t* sim_spawn_incident(struct sim* S, int force) {
    (void)force;
    if (S->n_incidents == S->cap_incidents) {
        size_t nc = S->cap_incidents * 2;
        sim_incident_t* ni = (sim_incident_t*)realloc(S->incidents, nc * sizeof(sim_incident_t));
        if (!ni) return NULL;
        S->incidents = ni;
        S->cap_incidents = nc;
    }
    sim_incident_t* inc = &S->incidents[S->n_incidents++];
    memset(inc, 0, sizeof(*inc));
    inc->id = S->next_incident_id++;

    double r = sim_rand01(S);
    if (r < 0.45) inc->type = SIM_INC_MEDICAL;
    else if (r < 0.65) inc->type = SIM_INC_FIRE;
    else inc->type = SIM_INC_CRIME;

    inc->severity = (sim_severity_t)(1 + sim_rand_int(S, 0, 2));
    inc->node = random_main_cc_node(S);
    inc->x = S->nodes[inc->node].x;
    inc->y = S->nodes[inc->node].y;
    inc->spawn_time = S->sim_time;
    inc->assigned_unit = -1;
    inc->resolved = 0;
    inc->response_time = 0.0;

    tree_bplus_insert(S->incidents_log, (ds_key_t)inc->id, inc);
    tree_rb_insert(S->pending_by_spawn, inc->spawn_time + (double)inc->id * 1e-9, inc);
    tree_threaded_insert(S->pending_alt, inc->spawn_time + (double)inc->id * 1e-9, inc);
    depq_push(S->pending_depq, (ds_key_t)inc->severity, inc);
    S->pq_operations++;

    /* event history */
    S->plist_head = misc_plist_push(S->plist, S->plist_head, inc);

    /* segment tree rolling counts */
    int bucket = (int)(((long long)S->sim_time) % 60);
    sp_seg_update(S->seg_incidents60, (size_t)bucket, 1);

    sim_log_append(S, "[t=%.1f] incident#%d %s sev=%d @ %s\n",
                   S->sim_time, inc->id, inc_type_name(inc->type),
                   (int)inc->severity, S->nodes[inc->node].street);
    return inc;
}

/* PATH PLANNING via DIJKSTRA shortest path on the road graph. Computes the
 * full predecessor array, reconstructs the node sequence, and stores it on
 * the unit so subsequent ticks can step along it without re-running Dijkstra. */
static void start_unit_path(struct sim* S, sim_unit_t* u, int target, int incident_id) {
    double* dist = (double*)malloc(sizeof(double) * S->n_nodes);
    int*    prev = (int*)malloc(sizeof(int) * S->n_nodes);
    if (!dist || !prev) { free(dist); free(prev); return; }

    double d = sim_dijkstra(S, u->cur_node, target, dist, prev);
    if (!(d < INFINITY)) {
        free(dist); free(prev);
        return;
    }
    u->path_len = sim_reconstruct_path(u->cur_node, target, prev, u->path, SIM_MAX_PATH);
    u->path_idx = 1; /* skip cur */
    u->target_node = target;
    u->incident_id = incident_id;
    if (incident_id >= 0) {
        u->state = SIM_UNIT_ENROUTE;
        u->eta = d / 64.0;
    } else {
        u->state = SIM_UNIT_RETURNING;
        u->eta = d / 64.0;
    }
    free(dist); free(prev);
}

/* INCIDENT STATE TRANSITION on arrival: ENROUTE -> ONSCENE (and arms an
 * on-scene timer whose duration scales with severity), or RETURNING -> IDLE.
 * Centralizes the FSM edges that fire when a unit reaches its target. */
void sim_unit_on_arrive(struct sim* S, sim_unit_t* u) {
    u->cur_node = u->target_node;
    u->target_node = -1;
    u->path_len = 0;
    u->path_idx = 0;
    u->eta = 0.0;

    if (u->state == SIM_UNIT_ENROUTE) {
        /* reached incident: transition to ONSCENE */
        int sev = 1;
        sim_incident_t* inc = NULL;
        if (u->incident_id >= 0 && (size_t)u->incident_id < S->n_incidents) {
            inc = &S->incidents[u->incident_id];
            sev = (int)inc->severity;
        }
        u->state = SIM_UNIT_ONSCENE;
        int hold = 4 - sev;
        if (hold < 2) hold = 2;
        u->on_scene_remaining = (double)hold;
    } else if (u->state == SIM_UNIT_RETURNING) {
        /* back at station */
        u->state = SIM_UNIT_IDLE;
        u->incident_id = -1;
        /* quad/kd indices rebuilt per-tick. */
    }
}

/* PATH-FOLLOWING STEP + ON-SCENE TIMER: each tick the unit either counts down
 * its on-scene timer (resolving the incident and removing it from the RB +
 * threaded BST pending indices when it hits zero), or advances along its
 * precomputed Dijkstra path by speed*dt distance, possibly crossing several
 * graph nodes per tick. */
static void advance_unit(struct sim* S, sim_unit_t* u, double dt) {
    if (u->state == SIM_UNIT_IDLE) return;
    if (u->state == SIM_UNIT_ONSCENE) {
        u->on_scene_remaining -= dt;
        if (u->on_scene_remaining <= 0.0) {
            /* resolve incident */
            if (u->incident_id >= 0) {
                sim_incident_t* inc = &S->incidents[u->incident_id];
                if (!inc->resolved) {
                    inc->resolved = 1;
                    inc->response_time = S->sim_time - inc->spawn_time;
                    S->total_response_time += inc->response_time;
                    S->resolved_count++;
                    sim_log_append(S, "[t=%.1f] incident#%d resolved rt=%.1f\n",
                                   S->sim_time, inc->id, inc->response_time);
                    tree_rb_delete(S->pending_by_spawn,
                                   inc->spawn_time + (double)inc->id * 1e-9);
                    tree_threaded_delete(S->pending_alt,
                                         inc->spawn_time + (double)inc->id * 1e-9);
                    S->plist_head = misc_plist_push(S->plist, S->plist_head, inc);
                }
            }
            /* return to station */
            int home = S->stations[u->station_id].node;
            start_unit_path(S, u, home, -1);
        }
        return;
    }

    /* ENROUTE or RETURNING: move along path at 64 units/sec */
    if (u->path_len == 0 || u->path_idx >= u->path_len) {
        sim_unit_on_arrive(S, u);
        return;
    }

    double speed = 64.0;
    double remaining = speed * dt;
    while (remaining > 0.0 && u->path_idx < u->path_len) {
        int nn = u->path[u->path_idx];
        double nx = S->nodes[nn].x;
        double ny = S->nodes[nn].y;
        double dx = nx - u->x, dy = ny - u->y;
        double d = sqrt(dx * dx + dy * dy);
        if (d <= remaining) {
            u->x = nx; u->y = ny;
            u->cur_node = nn;
            u->path_idx++;
            remaining -= d;
        } else {
            u->x += dx * (remaining / d);
            u->y += dy * (remaining / d);
            remaining = 0.0;
        }
    }
    if (u->eta > dt) u->eta -= dt;
    else u->eta = 0.0;

    if (u->path_idx >= u->path_len) sim_unit_on_arrive(S, u);
}

/* Per-tick driver: advances every unit's lifecycle by dt. */
void sim_tick_units(struct sim* S, double dt) {
    for (size_t i = 0; i < S->n_units; i++) advance_unit(S, &S->units[i], dt);
}

/* Rebuild idle-unit quadtree each tick. */
void sim_rebuild_idle_quad(struct sim* S);
void sim_rebuild_idle_quad(struct sim* S) {
    if (S->q_idle) sp_quad_destroy(S->q_idle);
    double w = (double)S->cols * 64.0 + 64.0;
    double h = (double)S->rows * 64.0 + 64.0;
    S->q_idle = sp_quad_create(0.0, 0.0, w, h);
    if (!S->q_idle) return;
    for (size_t i = 0; i < S->n_units; i++) {
        sim_unit_t* u = &S->units[i];
        if (u->state == SIM_UNIT_IDLE) {
            sp_quad_insert(S->q_idle, u->x, u->y, (ds_val_t)(intptr_t)u->id);
        }
    }
}

/* dispatcher */
/* DISPATCH: find the closest idle unit of the right type to an incident.
 * Uses the QUADTREE for spatial nearest-candidates, AVL for id->unit lookup,
 * DIJKSTRA for true road-distance ranking, then updates many indices on
 * commit (RB/threaded delete from pending, SPLAY insert into recent cache,
 * SKIP LIST eta board, TREAP station load counter). */
int sim_try_dispatch(struct sim* S, sim_incident_t* inc) {
    if (!inc || inc->assigned_unit >= 0 || inc->resolved) return -1;
    sim_unit_type_t want = unit_for_incident(inc->type);

    sp_point_t cand[16];
    size_t nc = sp_quad_nearest(S->q_idle, inc->x, inc->y, 16, cand);

    double best_cost = INFINITY;
    int best_uid = -1;
    int best_path[SIM_MAX_PATH];
    int best_plen = 0;

    double* dist = (double*)malloc(sizeof(double) * S->n_nodes);
    int*    prev = (int*)malloc(sizeof(int) * S->n_nodes);
    if (!dist || !prev) { free(dist); free(prev); return -1; }

    int checked = 0;
    for (size_t i = 0; i < nc && checked < 8; i++) {
        int uid = (int)(intptr_t)cand[i].data;
        ds_val_t v;
        if (tree_avl_get(S->unit_by_id, (ds_key_t)uid, &v) != DS_OK) continue;
        sim_unit_t* u = (sim_unit_t*)v;
        if (!u || u->type != want || u->state != SIM_UNIT_IDLE) continue;
        checked++;

        double d = sim_dijkstra(S, u->cur_node, inc->node, dist, prev);
        if (d < best_cost) {
            best_cost = d;
            best_uid = uid;
            best_plen = sim_reconstruct_path(u->cur_node, inc->node, prev, best_path, SIM_MAX_PATH);
        }
    }

    free(dist); free(prev);

    if (best_uid < 0) return -1;

    ds_val_t v;
    tree_avl_get(S->unit_by_id, (ds_key_t)best_uid, &v);
    sim_unit_t* u = (sim_unit_t*)v;
    memcpy(u->path, best_path, sizeof(int) * (size_t)best_plen);
    u->path_len = best_plen;
    u->path_idx = 1;
    u->target_node = inc->node;
    u->incident_id = inc->id;
    u->state = SIM_UNIT_ENROUTE;
    u->eta = best_cost / 64.0;

    inc->assigned_unit = u->id;
    /* remove from pending lists (keep in rb if not resolved; but assigned is fine). */
    tree_rb_delete(S->pending_by_spawn,
                   inc->spawn_time + (double)inc->id * 1e-9);
    tree_threaded_delete(S->pending_alt,
                         inc->spawn_time + (double)inc->id * 1e-9);

    tree_splay_insert(S->recent_units, (ds_key_t)u->id, u);
    rnd_skip_insert(S->eta_board, u->eta, (ds_val_t)(intptr_t)u->id);
    /* bump station workload */
    ds_val_t lv;
    if (rnd_treap_get(S->station_load, (ds_key_t)u->station_id, &lv) == DS_OK) {
        intptr_t cur = (intptr_t)lv;
        rnd_treap_delete(S->station_load, (ds_key_t)u->station_id);
        rnd_treap_insert(S->station_load, (ds_key_t)u->station_id,
                         (ds_val_t)(intptr_t)(cur + 1));
    }

    sim_log_append(S, "[t=%.1f] %s-%02d dispatched to %s@%s sev=%d\n",
                   S->sim_time, unit_type_prefix(u->type), u->id,
                   inc_type_name(inc->type), S->nodes[inc->node].street,
                   (int)inc->severity);
    return 0;
}
