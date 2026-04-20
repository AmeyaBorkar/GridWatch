# Emergency Dispatch Simulator

A C11 library + terminal app built around the syllabus for **Advanced Data Structures**.
The simulation models a grid city where ambulances, fire trucks, and police units
are dispatched to emergencies using the data structures from every unit of the
syllabus.

```
      ┌─────────────┐
      │    TUI      │  src/tui/*.c      (terminal frontend)
      └──────┬──────┘
             │   includes only dispatch/sim.h — the public contract
      ┌──────▼──────┐
      │    sim      │  src/sim/*.c      (city, units, dispatcher)
      └──────┬──────┘
             │   uses every DS module below
      ┌──────▼──────┐
      │  libdispatch│  src/ds/**/*.c
      │  heaps      │
      │  trees      │
      │  strings    │
      │  randomized │
      │  spatial    │
      │  misc       │
      └─────────────┘
```

The core library is deliberately **frontend-agnostic** — the same
`libdispatch.dll` can be bound from Python (`ctypes` / `cffi`) or served
behind a small web API later without touching the simulation or DS code.

## Build

Requires a C11 compiler (MSYS2 GCC tested) and GNU make.

```
make            # builds build/libdispatch.a and build/dispatch.exe (the TUI)
make shared     # also builds build/libdispatch.dll for FFI
make tests      # builds tests/test_*.exe (per-module unit tests)
make run        # launches the TUI
make clean
```

## Syllabus → code map

| Unit | Topic | File(s) | Used in simulation for |
|------|-------|---------|------------------------|
| 1 | **Threaded BT** | `src/ds/trees/threaded.c` | Stack-free in-order walk of the pending-incident tree for replay |
| 1 | **AVL Tree** | `src/ds/trees/avl.c` | Unit roster keyed by unit id |
| 1 | **Red-Black Tree** | `src/ds/trees/rb.c` | Pending-incident index keyed by spawn time |
| 1 | **Huffman Tree** | `src/ds/trees/huffman.c` | Radio-log compression ratio |
| 1 | **B+ Tree** | `src/ds/trees/bplus.c` | Incident log index, range scans |
| 1 | **Splay Tree** | `src/ds/trees/splay.c` | Recently-dispatched unit cache |
| 2 | **Double-Ended PQ** | `src/ds/heaps/depq.c` | Pending-incident queue, pops highest severity |
| 2 | **Leftist Tree** | `src/ds/heaps/leftist.c` | Meldable priority-queue demo |
| 2 | **Binomial Heap** | `src/ds/heaps/binom.c` | Alternative PQ, merge operation |
| 2 | **Fibonacci Heap** | `src/ds/heaps/fib.c` | **Dijkstra routing** — amortized O(1) decrease-key |
| 2 | **Skew Heap** | `src/ds/heaps/skew.c` | Meldable PQ demo |
| 2 | **Pairing Heap** | `src/ds/heaps/pairing.c` | Meldable PQ demo |
| 3 | **DAWG** | `src/ds/strings/dawg.c` | Minimal automaton for the street dictionary |
| 3 | **Trie** | `src/ds/strings/trie.c` | Street-name autocomplete |
| 3 | **Compressed Trie** | `src/ds/strings/crtrie.c` | Radix-compressed dictionary demo |
| 3 | **Suffix Array** | `src/ds/strings/sa.c` | Pattern search over the radio log |
| 3 | **Fuzzy Dictionary** | `src/ds/strings/fuzzy.c` | Edit-distance tolerant place search (BK-tree) |
| 4 | **Skip List** | `src/ds/randomized/skip.c` | ETA leaderboard |
| 4 | **Treap** | `src/ds/randomized/treap.c` | Per-station workload ranking |
| 5 | **Quadtree** | `src/ds/spatial/quad.c` | Nearest idle unit to an incident |
| 5 | **Octree** | — | *2D world; quadtree replaces it* |
| 5 | **Region queries** | `src/ds/spatial/quad.c` | Viewport/region filters |
| 5 | **Interval Tree** | `src/ds/spatial/itree.c` | Unit shift intervals (stabbing query at "now") |
| 5 | **Segment Tree** | `src/ds/spatial/seg.c` | Rolling 60-second incident counts |
| 5 | **Range Tree** | `src/ds/spatial/range.c` | 2D orthogonal range reporting (UI filters) |
| 5 | **Priority Search Tree** | `src/ds/heaps/*` + range | *Covered via DEPQ + range tree combination* |
| 5 | **BSP Tree** | `src/ds/spatial/bsp.c` | Static partition of station locations |
| 5 | **R-Tree** | `src/ds/spatial/rtree.c` | Station coverage rectangles |
| 6 | **Big Table** | — | *Persistent-DS concept demonstrated via the log + plist* |
| 6 | **DSU (Union-Find)** | `src/ds/misc/dsu.c` | Connected road components (a blocked road may partition the city) |
| 6 | **Concurrent DS** | — | *Single-threaded sim; noted as design extension* |
| 6 | **Succinct bitvector** | `src/ds/misc/bv.c` | Per-node `has_incident` flag with rank1/select1 |
| 6 | **Tree representation** | `src/ds/misc/bv.c` | Succinct-style tree-bit pattern (LOUDS-adjacent) |
| 6 | **Persistent DS** | `src/ds/misc/plist.c` | Immutable event history for undo/replay |
| 6 | **Cache-Oblivious DS** | — | *Design note — B+ tree exhibits cache-friendly block layout* |

Supplementary randomised data structure bonuses: `rnd_seed()` exposes a
splitmix64 stream so experiments are reproducible; the heaps module ships
a full suite for benchmarking Dijkstra against alternate priority queues.

## Public API

Include a single header:

```c
#include "dispatch/dispatch.h"
```

All structures expose **opaque handles** with plain-C `create` / `destroy`
pairs and keys/values as `double` / `void*`. Every public symbol is marked
with the `DS_API` macro so the library builds cleanly as a static archive,
a shared DLL, or a future WASM target.

### Simulation API

```c
sim_t *sim = sim_create(rows, cols, seed);
while (!quit) {
    sim_tick(sim, dt);
    sim_unit_view_t units[256];
    size_t n = sim_units(sim, units, 256);
    /* render ... */
}
sim_destroy(sim);
```

See `include/dispatch/sim.h` for the full list of snapshot structs and
functions.

## FFI: driving the library from Python

```bash
make shared     # produces build/libdispatch.dll
```

```python
import ctypes as C
lib = C.CDLL("./build/libdispatch.dll")

lib.sim_create.restype = C.c_void_p
lib.sim_create.argtypes = [C.c_int, C.c_int, C.c_uint64]
lib.sim_tick.argtypes  = [C.c_void_p, C.c_double]
lib.sim_destroy.argtypes = [C.c_void_p]

sim = lib.sim_create(10, 12, 42)
for _ in range(600):
    lib.sim_tick(sim, 0.1)
lib.sim_destroy(sim)
```

A thin Python wrapper exposing `sim_units`, `sim_incidents`, etc. can power
a pygame or web frontend without any C changes.

## Project layout

```
include/dispatch/     public headers (one per module + dispatch.h facade)
src/ds/<module>/      data-structure implementations
src/sim/              simulation core
src/tui/              terminal UI (calls only into sim.h)
tests/                per-module unit tests
Makefile
```

## Tests

```
make tests
./build/test_heaps.exe
./build/test_trees.exe
./build/test_strings.exe
./build/test_randomized.exe
./build/test_spatial.exe
./build/test_misc.exe
./build/test_sim.exe
```

Each test binary prints `PASS` and exits 0 on success.
