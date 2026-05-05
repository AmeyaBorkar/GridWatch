/* sim_internal.h — private types shared across simulation sources */
#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

#include "dispatch/sim.h"
#include "dispatch/heaps.h"
#include "dispatch/trees.h"
#include "dispatch/strings.h"
#include "dispatch/randomized.h"
#include "dispatch/spatial.h"
#include "dispatch/misc.h"

#include <stddef.h>
#include <stdint.h>

#define SIM_STREET_POOL_SZ 24
#define SIM_LOG_CAP        (1 << 16)   /* 64 KiB rolling radio log */
#define SIM_SA_REBUILD_EVERY  32
#define SIM_HUFF_REBUILD_EVERY 256
#define SIM_MAX_PATH       256

extern const char* const SIM_STREETS[SIM_STREET_POOL_SZ];

typedef struct {
    int    id;
    int    row, col;
    double x, y;
    char   street[48];
    int    in_main_cc;
} sim_node_t;

typedef struct {
    int    id;
    int    from_node, to_node;
    double length;
    int    blocked;
} sim_road_t;

typedef struct sim_unit {
    int              id;
    int              station_id;
    sim_unit_type_t  type;
    sim_unit_state_t state;
    int              cur_node;
    int              target_node;
    double           x, y;
    int              incident_id;
    double           eta;
    /* path as sequence of node ids; idx = next waypoint */
    int              path[SIM_MAX_PATH];
    int              path_len;
    int              path_idx;
    double           on_scene_remaining;
    double           shift_start, shift_end;
} sim_unit_t;

typedef struct sim_station {
    int             id;
    sim_unit_type_t type;
    int             node;
    double          x, y;
    int             n_units;
    char            name[48];
} sim_station_t;

typedef struct sim_incident {
    int             id;
    sim_inc_type_t  type;
    sim_severity_t  severity;
    int             node;
    double          x, y;
    double          spawn_time;
    int             assigned_unit;
    int             resolved;
    double          response_time;
} sim_incident_t;

typedef struct {
    char*  buf;
    size_t len;
    size_t cap;
    size_t writes_since_sa;
    size_t writes_since_huff;
    huffman_t* huff;
    str_sa_t*  sa;
} sim_log_t;

/* The full simulation state. This is the concrete struct hidden behind the
 * opaque `sim_t` typedef in the public header. Each group below corresponds
 * to one ADS concept used by the dispatcher. */
struct sim {
    int rows, cols;
    uint64_t seed;
    uint64_t prng_state;
    double sim_time;
    int paused;
    double spawn_rate;
    double spawn_accum;

    /* nodes/roads — the raw graph: array of intersections + array of edges. */
    sim_node_t* nodes;
    size_t      n_nodes;
    sim_road_t* roads;
    size_t      n_roads;

    /* CSR (Compressed Sparse Row) adjacency: flat arrays for graph traversal.
     * adj_head[u] indexes into adj_to/adj_w to find u's outgoing edges. This is
     * the cache-friendly representation Dijkstra walks during edge relaxation. */
    int*    adj_head;   /* size n_nodes+1 */
    int*    adj_to;     /* size 2*n_roads */
    double* adj_w;      /* size 2*n_roads */
    int*    adj_blocked; /* parallel array — flag per directed edge for closures */

    /* stations, units, incidents */
    sim_station_t* stations;
    size_t         n_stations;
    sim_unit_t*    units;
    size_t         n_units;
    size_t         cap_units;
    sim_incident_t* incidents;
    size_t          n_incidents;
    size_t          cap_incidents;
    int             next_unit_id;
    int             next_incident_id;

    /* DSU (Disjoint-Set Union / union-find): tracks connected components of
     * the road graph. road_components is the count shown in the HUD; the
     * main_cc_root identifies the largest component that the city uses. */
    misc_dsu_t* dsu;
    size_t      road_components;
    int         main_cc_root;

    /* log */
    sim_log_t log;

    /* Balanced/specialized BST family — each indexes a different view of state.
     * AVL = strict balance (unit lookup), RB = relaxed balance (FIFO pending),
     * splay = move-to-root for temporal locality, B+ = bulk-friendly id log,
     * threaded = in-order traversal without recursion. */
    tree_avl_t*    unit_by_id;        /* id -> sim_unit_t* */
    tree_rb_t*     pending_by_spawn;  /* spawn_time -> sim_incident_t* */
    tree_splay_t*  recent_units;      /* id -> sim_unit_t* */
    tree_bplus_t*  incidents_log;     /* id -> sim_incident_t* */
    tree_threaded_t* pending_alt;     /* mirror of pending by spawn_time */

    /* Heap zoo: kept around as the alternative meldable PQs (binomial, leftist,
     * skew, pairing). The DEPQ (double-ended priority queue) is the real worker
     * here — it orders pending incidents by severity so high-priority calls
     * jump the queue. (Note: the Fib heap lives transiently inside Dijkstra.) */
    heap_binom_t*   h_binom;
    heap_leftist_t* h_leftist;
    heap_skew_t*    h_skew;
    heap_pairing_t* h_pairing;
    depq_t*         pending_depq;     /* severity-ordered pending queue */

    /* String indexes for the search bar: trie + compressed-radix trie + DAWG
     * for prefix completion, BK-tree-style fuzzy matcher for typo tolerance. */
    str_trie_t*   trie;
    str_crtrie_t* crtrie;
    str_dawg_t*   dawg;
    str_fuzzy_t*  fuzzy;

    /* Randomized DS: skip list (probabilistic balanced BST) for the live ETA
     * leaderboard; treap (BST + heap by random priority) for station load. */
    rnd_skip_t*   eta_board;      /* ETA leaderboard */
    rnd_treap_t*  station_load;

    /* Spatial indexes — each a different geometric query strategy.
     * quadtree    = nearest idle unit (lazy 4-way split),
     * kd-tree     = generic 2D nearest-neighbor,
     * R-tree      = station coverage rectangles,
     * segment tree = 60-bucket rolling incident counts (range sum),
     * interval tree = unit shift overlap queries,
     * range tree  = orthogonal range search,
     * BSP         = binary space partition over stations. */
    sp_quad_t*  q_idle;           /* idle units */
    sp_kd_t*    kd_units;         /* rebuilt per frame (optional) */
    sp_rtree_t* rt_stations;      /* coverage rects */
    sp_seg_t*   seg_incidents60;  /* 60-bucket rolling counts */
    sp_itree_t* itree_shifts;     /* unit shift intervals */
    sp_range_t* range_units;      /* built once; for UI range queries */
    sp_bsp_t*   bsp_stations;

    /* Misc DS: persistent (immutable) linked list of past events for time-travel
     * inspection, plus a bitvector marking which nodes currently host incidents. */
    misc_plist_t*  plist;
    misc_pnode_t*  plist_head;
    misc_bv_t*     bv_has_incident;

    /* Counters surfaced as HUD metrics — every priority-queue op and every
     * Dijkstra invocation is tallied so the user can watch the DS work live. */
    size_t pq_operations;
    size_t dispatch_calls;
    double total_response_time;
    size_t resolved_count;

    /* rolling seg bucket */
    int seg_cur_bucket;
    double seg_bucket_accum;
};

/* ---- prng / small helpers ---- */
uint64_t sim_splitmix64(uint64_t* state);
double   sim_rand01(struct sim* S);
int      sim_rand_int(struct sim* S, int lo, int hi);  /* inclusive */

/* ---- city building ---- */
int  sim_build_city(struct sim* S);
void sim_free_city(struct sim* S);
int  sim_node_index(const struct sim* S, int r, int c);

/* ---- entities ---- */
int  sim_init_stations_units(struct sim* S);
void sim_free_entities(struct sim* S);
sim_incident_t* sim_spawn_incident(struct sim* S, int force);
void sim_tick_units(struct sim* S, double dt);
void sim_unit_on_arrive(struct sim* S, sim_unit_t* u);

/* ---- routing ---- */
/* Runs Dijkstra from src; fills dist[] and prev[]. Returns dist to tgt or INFINITY. */
double sim_dijkstra(struct sim* S, int src, int tgt,
                    double* dist, int* prev);
/* Reconstructs path from prev[] into out[] backwards; returns length. */
int    sim_reconstruct_path(int src, int tgt, const int* prev, int* out, int cap);

/* ---- dispatcher ---- */
int sim_try_dispatch(struct sim* S, sim_incident_t* inc);

/* ---- log ---- */
void sim_log_init(sim_log_t* L);
void sim_log_free(sim_log_t* L);
void sim_log_append(struct sim* S, const char* fmt, ...);

/* ---- search ---- */
int sim_search_init(struct sim* S);
void sim_search_free(struct sim* S);

/* ---- metrics ---- */
void sim_metrics_tick(struct sim* S, double dt);

#endif
