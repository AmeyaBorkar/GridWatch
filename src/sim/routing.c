#include "sim_internal.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* The below function is Dijkstra's single-source shortest-path algorithm,
 * backed by a Fibonacci heap as its priority queue. Used every time the
 * dispatcher routes a unit to an incident — the cornerstone of the sim. */
double sim_dijkstra(struct sim* S, int src, int tgt,
                    double* dist, int* prev) {
    size_t n = S->n_nodes;
    /* Init phase: dist = +infinity for every node, prev = none. The classic
     * Dijkstra invariant — a node's dist is "best known so far", improved
     * monotonically as edges are relaxed. */
    for (size_t i = 0; i < n; i++) { dist[i] = INFINITY; prev[i] = -1; }
    if (src < 0 || (size_t)src >= n) return INFINITY;
    dist[src] = 0.0;

    /* Create the Fibonacci-heap priority queue. Chosen over a binary heap
     * specifically because of its O(1) amortized decrease-key (see below). */
    heap_fib_t* pq = heap_fib_create();
    if (!pq) return INFINITY;

    /* Handles array: keyed by node id, stores the heap-node pointer for each
     * node currently in the PQ. KEY DETAIL — without this we can't call
     * decrease_key, because the Fib heap takes a node handle, not a value.
     * NULL means "not in PQ" (either never inserted or already popped). */
    heap_fib_node_t** handles = (heap_fib_node_t**)calloc(n, sizeof(heap_fib_node_t*));
    if (!handles) { heap_fib_destroy(pq); return INFINITY; }

    handles[src] = heap_fib_push(pq, 0.0, (ds_val_t)(intptr_t)src);
    S->pq_operations++;
    S->dispatch_calls++;

    /* Main loop: repeatedly pop the closest unvisited node and relax its edges.
     * The lazy-deletion guard `e.key > dist[u]` skips stale entries left over
     * from an earlier push — cheaper than removing them eagerly. */
    while (heap_fib_size(pq) > 0) {
        ds_entry_t e;
        if (heap_fib_pop_min(pq, &e) != DS_OK) break;
        S->pq_operations++;
        int u = (int)(intptr_t)e.val;
        if (e.key > dist[u]) continue;   /* stale entry — newer one supersedes */
        handles[u] = NULL;
        if (tgt >= 0 && u == tgt) break;

        int s_idx = S->adj_head[u], e_idx = S->adj_head[u + 1];
        for (int k = s_idx; k < e_idx; k++) {
            if (S->adj_blocked[k]) continue;
            int v = S->adj_to[k];
            double nd = dist[u] + S->adj_w[k];
            /* The relaxation step: if going via u gives a shorter path to v,
             * update v's tentative distance. This is where shortest paths
             * actually emerge — the inductive heart of Dijkstra. */
            if (nd < dist[v]) {
                dist[v] = nd;
                prev[v] = u;
                /* Decrease-key vs push: if v is already in the PQ we lower
                 * its key in place (O(1) amortized — exactly why we chose
                 * the Fib heap); otherwise we push a fresh entry. A binary
                 * heap would have to push duplicates and rely on the stale
                 * guard above, which is correct but does more PQ work. */
                if (handles[v]) {
                    heap_fib_decrease_key(pq, handles[v], nd);
                    S->pq_operations++;
                } else {
                    handles[v] = heap_fib_push(pq, nd, (ds_val_t)(intptr_t)v);
                    S->pq_operations++;
                }
            }
        }
    }

    free(handles);
    heap_fib_destroy(pq);
    if (tgt < 0) return 0.0;
    return dist[tgt];
}

/* The below block reconstructs the actual path by walking the prev[] array
 * backwards from tgt to src, then reversing. prev[] is the predecessor map
 * Dijkstra builds as a side effect of relaxation — the path is implicit
 * in it and only materialized on demand. */
int sim_reconstruct_path(int src, int tgt, const int* prev, int* out, int cap) {
    if (tgt < 0 || src < 0) return 0;
    int buf[SIM_MAX_PATH];
    int n = 0;
    int cur = tgt;
    while (cur != -1 && n < SIM_MAX_PATH) {
        buf[n++] = cur;
        if (cur == src) break;
        cur = prev[cur];
    }
    if (n == 0 || buf[n - 1] != src) return 0;
    /* reverse into out */
    int w = 0;
    for (int i = n - 1; i >= 0 && w < cap; i--) out[w++] = buf[i];
    return w;
}
