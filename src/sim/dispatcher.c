#include "sim_internal.h"
#include <stdlib.h>

extern void sim_rebuild_idle_quad(struct sim* S);

/* DISPATCHER MAIN LOOP — DEPQ-DRIVEN SEVERITY SCHEDULING.
 * Each tick this drains a budgeted number of incidents from the pending DEPQ,
 * always pulling the HIGHEST-severity one first via depq_pop_max so that critical
 * calls (severity 5) are always handled before routine ones. The DEPQ also lets
 * the producer side drop the lowest-severity incident if the queue grows too big,
 * implementing graceful overload behaviour. For each popped incident,
 * sim_try_dispatch (a) uses the idle-units Quadtree for fast nearest-unit
 * candidate selection, then (b) calls Dijkstra in routing.c on the CSR graph
 * to compute the actual shortest road-route ETA. If no unit is currently free,
 * the incident is re-queued at the same severity so it competes again next tick. */
/* Process pending incidents, highest severity first (DEPQ). */
void sim_dispatch_pending(struct sim* S);
void sim_dispatch_pending(struct sim* S) {
    if (!S->pending_depq) return;

    /* Try a limited number per tick to avoid stalls. */
    int budget = 8;
    while (budget-- > 0 && depq_size(S->pending_depq) > 0) {
        ds_entry_t e;
        /* DEPQ POP-MAX: highest severity wins — the whole point of using a DEPQ
         * over a plain max-heap is paired with the producer's ability to
         * pop_min cheap incidents when the queue overflows. */
        if (depq_pop_max(S->pending_depq, &e) != DS_OK) break;
        S->pq_operations++;
        sim_incident_t* inc = (sim_incident_t*)e.val;
        if (!inc || inc->resolved || inc->assigned_unit >= 0) continue;

        /* sim_try_dispatch: scans candidate idle units (Quadtree-accelerated
         * spatial query) and runs Dijkstra on the CSR road graph to pick the
         * unit with the lowest actual travel time. */
        if (sim_try_dispatch(S, inc) != 0) {
            /* No unit available; re-queue with same severity for next round. */
            depq_push(S->pending_depq, (ds_key_t)inc->severity, inc);
            S->pq_operations++;
            break;
        }

        /* After assignment, idle set changed; rebuild quad for next candidate. */
        sim_rebuild_idle_quad(S);
    }
}
