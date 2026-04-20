# Dispatch Simulator — Web UI

A Python + Flask + vanilla-JS frontend for `libdispatch`, mirroring the
capabilities of the TUI on top of the same C ABI declared in
`include/dispatch/sim.h`.

```
web/
  dispatch_py/        Python ctypes wrapper around build/libdispatch.dll
  server.py           Flask server (owns the Sim + a background ticker thread)
  static/             index.html + app.css + app.js (zero-dependency frontend)
  test_wrapper.py     Self-test for the ctypes wrapper
  run.sh / run.bat    Build the DLL if missing, then start the server
```

## Prereqs

* Python 3.11+ with **Flask** installed (`pip install flask`)
* MSYS2 GCC (or any C11 compiler) to build `build/libdispatch.dll` via
  `make shared` from the project root.

## Run

From the project root:

```
make shared          # produces build/libdispatch.dll
python web/server.py # starts Flask on http://127.0.0.1:5000/
```

or simply `bash web/run.sh` / `web\run.bat`.

Open `http://127.0.0.1:5000/` in a browser — the page polls `/state` ten times
per second and renders the map on an HTML5 canvas.

## HTTP endpoints

| method | path            | purpose                                                     |
| ------ | --------------- | ----------------------------------------------------------- |
| GET    | `/`             | serves `static/index.html`                                  |
| GET    | `/static/<f>`   | Flask's built-in static handler                             |
| GET    | `/state`        | full JSON snapshot (nodes, roads, units, incidents, metrics)|
| POST   | `/tick`         | `{"dt": 0.1}` — advance manually (the server also auto-ticks)|
| POST   | `/control`      | `{"paused": true}` / `{"spawn_rate": 0.6}` / `{"force_spawn": true}` |
| GET    | `/search`       | `?q=Oa&fuzzy=0&max=10` — autocomplete / fuzzy dictionary     |
| GET    | `/log/count`    | `?pattern=AMB-03` — radio-log substring count (suffix array) |

## Python wrapper quickstart

```python
import sys; sys.path.insert(0, 'web')
from dispatch_py import Sim

with Sim(rows=10, cols=12, seed=42) as sim:
    for _ in range(600):
        sim.tick(0.1)
    print(sim.metrics())
    print(sim.autocomplete("Oa"))
```

## Env vars

* `LIBDISPATCH_DLL` — override the DLL path (default `build/libdispatch.dll`).
* `SIM_ROWS`, `SIM_COLS`, `SIM_SEED` — initial grid for the server.

## Notes

* All Sim calls are serialised behind a single `threading.Lock`; the
  background ticker and HTTP handlers share it.
* The wrapper lazy-loads the DLL inside `Sim.__init__`, so `import dispatch_py`
  and `import server` succeed even before `make shared` has been run.
* Zero external JS/CSS dependencies — vanilla JS, no bundler, no CDN.
