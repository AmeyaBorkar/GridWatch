/* sim.h — public simulation API for the Emergency Dispatch Simulator.
 *
 * This is the only header any UI (TUI, Python ctypes/cffi, Web server)
 * needs to include. All state is opaque; all views are POD snapshots.
 */
#ifndef DISPATCH_SIM_H
#define DISPATCH_SIM_H

#include "dispatch/common.h"
#include "dispatch/spatial.h"   /* for sp_point_t (map rendering) */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Enums
 * ================================================================ */
typedef enum {
    SIM_UNIT_AMBULANCE = 0,
    SIM_UNIT_FIRE      = 1,
    SIM_UNIT_POLICE    = 2
} sim_unit_type_t;

typedef enum {
    SIM_INC_MEDICAL = 0,   /* requires ambulance */
    SIM_INC_FIRE    = 1,   /* requires fire     */
    SIM_INC_CRIME   = 2    /* requires police   */
} sim_inc_type_t;

typedef enum {
    SIM_UNIT_IDLE      = 0,
    SIM_UNIT_ENROUTE   = 1,
    SIM_UNIT_ONSCENE   = 2,
    SIM_UNIT_RETURNING = 3
} sim_unit_state_t;

typedef enum {
    SIM_SEV_LOW  = 1,
    SIM_SEV_MED  = 2,
    SIM_SEV_HIGH = 3
} sim_severity_t;

/* ================================================================
 * POD view structs (safe to copy across FFI)
 * ================================================================ */
typedef struct {
    int    id;
    int    row, col;    /* grid coordinates */
    double x, y;        /* world coordinates (pixels/units) */
    char   street[48];  /* canonical name */
} sim_node_view_t;

typedef struct {
    int    id;
    int    from_node, to_node;
    double length;
    int    blocked;     /* 0 or 1 */
} sim_road_view_t;

typedef struct {
    int             id;
    sim_unit_type_t type;
    int             node;         /* home intersection */
    double          x, y;
    int             n_units;
    char            name[48];
} sim_station_view_t;

typedef struct {
    int              id;
    int              station_id;
    sim_unit_type_t  type;
    sim_unit_state_t state;
    int              cur_node;
    int              target_node; /* -1 if none */
    double           x, y;        /* interpolated for rendering */
    int              incident_id; /* -1 if idle */
    double           eta;         /* seconds to target; 0 if not moving */
} sim_unit_view_t;

typedef struct {
    int             id;
    sim_inc_type_t  type;
    sim_severity_t  severity;
    int             node;
    double          x, y;
    double          spawn_time;   /* sim seconds */
    int             assigned_unit;/* -1 unassigned */
    int             resolved;     /* 0/1 */
    double          response_time;/* filled when resolved */
} sim_incident_view_t;

typedef struct {
    double sim_time;
    size_t total_incidents;
    size_t resolved_incidents;
    size_t pending_incidents;
    size_t active_units;
    size_t idle_units;
    double avg_response_time;     /* seconds, over resolved */
    size_t road_components;       /* via DSU */
    size_t log_bytes;
    size_t log_bytes_huffman;     /* compressed size */
    double huffman_ratio;
    size_t sa_suffix_count;       /* # suffixes in radio-log SA */
    size_t dispatch_calls;        /* # Dijkstra runs performed */
    size_t pq_operations;         /* heap pushes+pops+decrease-keys */
} sim_metrics_t;

/* ================================================================
 * Lifecycle
 * ================================================================ */
typedef struct sim sim_t;

DS_API sim_t* sim_create(int rows, int cols, uint64_t seed);
DS_API void   sim_destroy(sim_t*);

/* Advance simulation by dt real-seconds. */
DS_API void   sim_tick(sim_t*, double dt);

/* Pause / resume. When paused, sim_tick is a no-op. */
DS_API void   sim_set_paused(sim_t*, int paused);
DS_API int    sim_is_paused(const sim_t*);

/* Incidents per second when unpaused. Default: 0.4. */
DS_API void   sim_set_spawn_rate(sim_t*, double per_second);

/* Force an incident to spawn immediately. Returns its id or -1. */
DS_API int    sim_force_spawn(sim_t*);

/* ================================================================
 * World snapshots (copy into caller buffer, return # written)
 * ================================================================ */
DS_API int    sim_rows(const sim_t*);
DS_API int    sim_cols(const sim_t*);
DS_API size_t sim_nodes(const sim_t*, sim_node_view_t* out, size_t max);
DS_API size_t sim_roads(const sim_t*, sim_road_view_t* out, size_t max);
DS_API size_t sim_stations(const sim_t*, sim_station_view_t* out, size_t max);
DS_API size_t sim_units(const sim_t*, sim_unit_view_t* out, size_t max);
DS_API size_t sim_incidents(const sim_t*, sim_incident_view_t* out, size_t max);

/* ================================================================
 * Search (powered by Trie + Fuzzy dict)
 * ================================================================ */
/* Returns malloc'd strings; caller frees each. */
DS_API size_t sim_autocomplete(const sim_t*, const char* prefix, char** out, size_t max);
DS_API size_t sim_fuzzy(const sim_t*, const char* q, int max_edits, char** out, size_t max);

/* Look up node id by canonical street name (exact). -1 if not found. */
DS_API int    sim_node_by_street(const sim_t*, const char* name);

/* ================================================================
 * Metrics + radio log
 * ================================================================ */
DS_API void   sim_metrics(const sim_t*, sim_metrics_t* out);

/* Copy the last `cap` characters of the radio log into buf (NUL-terminated).
   Returns bytes copied (excluding NUL). */
DS_API size_t sim_log_tail(const sim_t*, char* buf, size_t cap);

/* Search the radio log via the suffix array. Returns # occurrences. */
DS_API size_t sim_log_count(const sim_t*, const char* pattern);

#ifdef __cplusplus
}
#endif

#endif /* DISPATCH_SIM_H */
