#include "sim_internal.h"
#include <stdlib.h>

extern void sim_rebuild_idle_quad(struct sim* S);

/* Process pending incidents, highest severity first (DEPQ). */
void sim_dispatch_pending(struct sim* S);
void sim_dispatch_pending(struct sim* S) {
    if (!S->pending_depq) return;

    /* Try a limited number per tick to avoid stalls. */
    int budget = 8;
    while (budget-- > 0 && depq_size(S->pending_depq) > 0) {
        ds_entry_t e;
        if (depq_pop_max(S->pending_depq, &e) != DS_OK) break;
        S->pq_operations++;
        sim_incident_t* inc = (sim_incident_t*)e.val;
        if (!inc || inc->resolved || inc->assigned_unit >= 0) continue;

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
