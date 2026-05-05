#include "sim_internal.h"
#include <stdlib.h>
#include <string.h>

void sim_metrics_tick(struct sim* S, double dt) {
    /* The below block touches the SEGMENT TREE that holds rolling 60-second
     * incident counts (`seg_incidents60`). When the wall clock crosses into
     * a new 1-second bucket, we point-update the segment tree to zero-out
     * that bucket — so any later range query sums only the live window. */
    S->seg_bucket_accum += dt;
    if (S->seg_bucket_accum >= 1.0) {
        int new_bucket = (int)(((long long)S->sim_time) % 60);
        if (new_bucket != S->seg_cur_bucket) {
            /* zero out the next bucket we will be writing into */
            long long old = sp_seg_query(S->seg_incidents60,
                                         (size_t)new_bucket, (size_t)new_bucket);
            sp_seg_update(S->seg_incidents60, (size_t)new_bucket, -old);
            S->seg_cur_bucket = new_bucket;
        }
        S->seg_bucket_accum -= 1.0;
    }

    /* The below block rebuilds a SUCCINCT BITVECTOR (one bit per graph node,
     * set iff that node currently has an unresolved incident). The bv is
     * rebuilt and re-`build`'d each tick so its rank/select index stays in
     * sync, giving O(1) "is node i hot?" / "how many hot nodes up to i?"
     * lookups for downstream dispatch heuristics. */
    /* Rebuild the bit vector of "has_incident" per node (bv). */
    if (S->bv_has_incident) {
        misc_bv_destroy(S->bv_has_incident);
    }
    S->bv_has_incident = misc_bv_create(S->n_nodes ? S->n_nodes : 1);
    if (S->bv_has_incident) {
        for (size_t i = 0; i < S->n_incidents; i++) {
            sim_incident_t* inc = &S->incidents[i];
            if (!inc->resolved && inc->node >= 0 && (size_t)inc->node < S->n_nodes) {
                misc_bv_set(S->bv_has_incident, (size_t)inc->node, 1);
            }
        }
        misc_bv_build(S->bv_has_incident);
    }
}
