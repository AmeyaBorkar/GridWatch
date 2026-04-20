# Contributions

*A short history of how the Emergency Dispatch Simulator came together.*

**Team**

- Ameya Borkar
- Aditya Chimurkar
- Aarush Bakshi
- Ayush Agnihotri
- Arnav Gupta

---

## Chapter 1 — Picking the problem

The five of us met on day one with a blank whiteboard and an eight-unit
syllabus. We wanted a project that would actually *use* every data
structure rather than shelve them in a demo folder.

Ameya floated the idea of an **Emergency Dispatch Simulator** — a grid
city where ambulances, fire trucks, and police units race to spawning
incidents. We saw a Fibonacci heap under every Dijkstra call, a quadtree
over every nearest-unit lookup, tries on the radio console, DSU tracking
road closures. Aditya pointed out it also gave us a natural place for
double-ended priority queues (severity-first pending incidents), which
locked the design in.

We agreed on three ground rules before writing any code:

1. **C11**, library-first, no third-party deps in the core.
2. Every public function exposed through an **opaque handle**, so a
   Python or Web UI could bind the same DLL later.
3. Each of the five of us would own a roughly equal slice of the DS
   modules *and* one slice of the glue layers (sim / TUI / web).

## Chapter 2 — The shared contract

Ameya spent a long afternoon sketching `include/dispatch/common.h` and
the per-module header shapes. The decision to make **all keys `double`
and values `void*`** kept FFI simple later on, even if it cost us a bit
of static typing on the C side. Arnav pushed back on a single seed
source — we settled on `rnd_seed()` owned by the randomized module plus
an internal splitmix64 inside the simulator, which Arnav later used in
the web layer for deterministic replays.

Once the headers stabilised the work split naturally along syllabus units.

## Chapter 3 — Building the data structures

We divided the six DS modules so no two of us stepped on the same
file. Everyone implemented their share from scratch; everyone wrote the
module's unit tests.

- **Aditya Chimurkar** took **Unit 2 (heaps)** — the Binomial, Leftist,
  Skew, and Pairing heaps, plus the interval-style Double-Ended PQ that
  became the severity-ordered pending queue in the dispatcher. He also
  implemented **Disjoint Set Union** and the **Segment Tree** (our
  rolling 60-second incident counter). Aditya's melding proofs on the
  whiteboard saved us a lot of integration pain.

- **Aarush Bakshi** took most of **Unit 1 (advanced trees)**: the
  **AVL**, **Red-Black**, **Splay**, **B+ tree** (order-4, linked
  leaves, range scans), and the **Threaded BST** for stack-free in-order
  replay. He also wrote the **Interval Tree** (unit-shift scheduling) on
  the spatial side and the **Persistent list** used for the event
  history / undo-replay.

- **Ayush Agnihotri** owned **Unit 3 (strings)** end-to-end: the ASCII
  **Trie**, the **Compressed Radix Trie**, the prefix-doubling
  **Suffix Array** with Kasai LCP, the incrementally-minimised
  **DAWG**, and the BK-tree **Fuzzy Dictionary**. He fed the sim's
  `search.c` from the same four DSes and wired the radio-log pipeline
  (`log.c`, including suffix-array and Huffman rebuild cadences). Ayush
  also wrote the **succinct bit vector** that tracks per-node incident
  flags.

- **Arnav Gupta** handled **Unit 4 (randomized)** — the **Skip List**
  (ETA leaderboard) and the **Treap** (per-station workload ranking),
  with a shared splitmix64 RNG. He then owned roughly half of **Unit 5**:
  the **KD-tree**, the **R-tree** (station coverage rectangles), the
  **Range tree** (2D orthogonal search), and the **BSP Tree**. At the
  end of the project he also built the entire web layer (`web/`),
  turning the library into something anyone could drive from a browser.

- **Ameya Borkar**, as team lead, kept the shared pieces moving. He
  implemented the **Fibonacci heap** — the one with explicit node
  handles and cascading cuts that drives every Dijkstra call in the
  simulator — and the **Huffman** coder that reports the live
  compression ratio of the radio log. On the spatial side he wrote the
  **Quadtree** (the idle-unit nearest-neighbour lookup used by the
  dispatcher). Ameya also designed the public C ABI
  (`include/dispatch/common.h`, `sim.h`, the `dispatch.h` umbrella),
  wrote the `Makefile` and the `build.sh` fallback, and authored the
  top-level `README.md`.

## Chapter 4 — The simulation layer

Once the DS modules compiled with zero warnings we regrouped to build
`src/sim/`. This was a more collaborative stretch because every file
touches several modules:

- `src/sim/sim.c` and `src/sim/sim_internal.h` — **Ameya**. The opaque
  `struct sim`, the tick loop, the view-snapshot exporters that become
  JSON in the web layer.
- `src/sim/city.c` — **Aditya**. Grid generation, CSR adjacency, street
  naming, and the DSU sweep that computes road components.
- `src/sim/routing.c` — **Ameya**. Dijkstra with `heap_fib_decrease_key`
  and metric counters for `pq_operations` / `dispatch_calls`.
- `src/sim/dispatcher.c` — **Aditya**. DEPQ-driven severity scheduling
  and the unit-candidate scan.
- `src/sim/entities.c` — **Aarush**. Station / unit / incident
  lifecycles, path-following motion, on-scene timers.
- `src/sim/search.c` — **Ayush**. Populates the Trie / Compressed Trie /
  DAWG / Fuzzy dict from the generated street names.
- `src/sim/log.c` — **Ayush**. Rolling radio-log buffer, suffix-array
  and Huffman rebuild cadences, `sim_log_count` via SA.
- `src/sim/metrics.c` — **Arnav**. Segment-tree rolling counts, skip-list
  leaderboard, succinct bitvector rebuild per tick, plist event history.

## Chapter 5 — Two frontends, one library

The library could now run 60 seconds of simulated time in a unit test
(`test_sim.c`), but we wanted something we could *watch*.

- **Ameya** wrote the C **terminal UI** (`src/tui/main.c`,
  `src/tui/render.c`, `src/tui/tui.h`) — a double-buffered ANSI renderer
  with a live map, HUD, radio log, and search box. **Aarush** added the
  keyboard / input handling in `src/tui/input.c`, and **Aditya** did the
  Windows console VT-mode setup and atexit cleanup in
  `src/tui/console.c`.

- **Arnav** built the **Python web UI** on top of the same DLL: a
  `ctypes` wrapper under `web/dispatch_py/`, a Flask server (`web/server.py`)
  with a background 30 Hz ticker thread, and a single-page JS frontend
  (`web/static/`) that draws the city on a canvas and polls `/state` at
  10 Hz. Because the C ABI was already frozen, Arnav never had to ask
  anyone to touch the sim — the DLL was a plug-and-play dependency.

## Chapter 6 — Tests, review, polish

Every module landed with a test binary under `tests/`. Each person
wrote the tests for their own DS, and we cross-reviewed.

Pair review:

- Aditya ↔ Aarush — trees/heaps correctness.
- Ayush ↔ Arnav — strings/randomized/spatial edge cases.
- Ameya ↔ everyone — sim integration, ABI stability, docs.

The final polish — `README.md`, `build.sh`, the
syllabus-topic-to-file table, the FFI notes — was Ameya's; everyone
signed off on the wording.

---

## At-a-glance ownership

| Area | Primary | Files |
|------|---------|-------|
| Project design, public C ABI, build system, README | Ameya | `include/dispatch/common.h`, `sim.h`, `dispatch.h`, `Makefile`, `build.sh`, `README.md` |
| Fibonacci heap + Huffman + Quadtree | Ameya | `src/ds/heaps/fib.c`, `src/ds/trees/huffman.c`, `src/ds/spatial/quad.c` |
| Binomial / Leftist / Skew / Pairing / DEPQ | Aditya | `src/ds/heaps/binom.c`, `leftist.c`, `skew.c`, `pairing.c`, `depq.c` |
| DSU, Segment tree | Aditya | `src/ds/misc/dsu.c`, `src/ds/spatial/seg.c` |
| AVL / RB / Splay / B+ / Threaded BST | Aarush | `src/ds/trees/avl.c`, `rb.c`, `splay.c`, `bplus.c`, `threaded.c` |
| Interval tree, Persistent list | Aarush | `src/ds/spatial/itree.c`, `src/ds/misc/plist.c` |
| Trie / Compressed Trie / Suffix Array / DAWG / Fuzzy | Ayush | `src/ds/strings/*.c` |
| Succinct bit vector | Ayush | `src/ds/misc/bv.c` |
| Skip List / Treap / shared RNG | Arnav | `src/ds/randomized/*.c` |
| KD-tree / R-tree / Range tree / BSP | Arnav | `src/ds/spatial/kd.c`, `rtree.c`, `range.c`, `bsp.c` |
| Sim core: `sim.c`, `routing.c`   | Ameya | `src/sim/sim.c`, `src/sim/routing.c`, `src/sim/sim_internal.h` |
| Sim: `city.c`, `dispatcher.c`    | Aditya | `src/sim/city.c`, `src/sim/dispatcher.c` |
| Sim: `entities.c`                | Aarush | `src/sim/entities.c` |
| Sim: `search.c`, `log.c`         | Ayush  | `src/sim/search.c`, `src/sim/log.c` |
| Sim: `metrics.c`                 | Arnav  | `src/sim/metrics.c` |
| TUI renderer + main loop         | Ameya  | `src/tui/main.c`, `src/tui/render.c`, `src/tui/tui.h` |
| TUI input                        | Aarush | `src/tui/input.c` |
| TUI console / VT setup           | Aditya | `src/tui/console.c` |
| Python + Flask web UI            | Arnav  | `web/dispatch_py/`, `web/server.py`, `web/static/`, `web/run.sh`, `web/run.bat`, `web/test_wrapper.py`, `web/README.md` |

Every member also wrote the unit tests for the modules they owned
(`tests/test_heaps.c`, `test_trees.c`, `test_strings.c`,
`test_randomized.c`, `test_spatial.c`, `test_misc.c`, `test_sim.c`).
