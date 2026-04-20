#include "sim_internal.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

double sim_dijkstra(struct sim* S, int src, int tgt,
                    double* dist, int* prev) {
    size_t n = S->n_nodes;
    for (size_t i = 0; i < n; i++) { dist[i] = INFINITY; prev[i] = -1; }
    if (src < 0 || (size_t)src >= n) return INFINITY;
    dist[src] = 0.0;

    heap_fib_t* pq = heap_fib_create();
    if (!pq) return INFINITY;

    heap_fib_node_t** handles = (heap_fib_node_t**)calloc(n, sizeof(heap_fib_node_t*));
    if (!handles) { heap_fib_destroy(pq); return INFINITY; }

    handles[src] = heap_fib_push(pq, 0.0, (ds_val_t)(intptr_t)src);
    S->pq_operations++;
    S->dispatch_calls++;

    while (heap_fib_size(pq) > 0) {
        ds_entry_t e;
        if (heap_fib_pop_min(pq, &e) != DS_OK) break;
        S->pq_operations++;
        int u = (int)(intptr_t)e.val;
        if (e.key > dist[u]) continue;
        handles[u] = NULL;
        if (tgt >= 0 && u == tgt) break;

        int s_idx = S->adj_head[u], e_idx = S->adj_head[u + 1];
        for (int k = s_idx; k < e_idx; k++) {
            if (S->adj_blocked[k]) continue;
            int v = S->adj_to[k];
            double nd = dist[u] + S->adj_w[k];
            if (nd < dist[v]) {
                dist[v] = nd;
                prev[v] = u;
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
