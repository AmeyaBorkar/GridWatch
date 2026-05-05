"""Flask front-end for libdispatch.

Usage:
    python web/server.py [--host 127.0.0.1] [--port 5000]

The server owns a single :class:`Sim` instance behind a lock and a background
ticker thread that advances the simulation at ~30 Hz so HTTP clients can simply
poll ``/state`` without having to drive ticks themselves.
"""

from __future__ import annotations

import argparse
import os
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional

from flask import Flask, jsonify, request, send_from_directory

# Make the sibling `dispatch_py` package importable when running directly.
_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

# FFI BRIDGE: `dispatch_py` is the ctypes wrapper around libdispatch.dll.
# Importing Sim here is what lets this Python process drive the C ABI directly
# (no subprocess, no IPC) — every Sim method below ultimately becomes a C call.
from dispatch_py import (  # noqa: E402
    DispatchError,
    Sim,
)


STATIC_DIR = _HERE / "static"

app = Flask(__name__, static_folder=str(STATIC_DIR), static_url_path="/static")


# ---------------------------------------------------------------------------
# Managed simulation + background ticker
# ---------------------------------------------------------------------------
# BACKGROUND TICKER THREAD: owns the single C-side Sim handle behind a lock
# and advances it at TICK_HZ via sim_tick(dt). Decoupling simulation time from
# HTTP requests lets the browser merely poll /state to get a fresh snapshot.
class SimRunner:
    """Owns the Sim, its lock, and a background ticker thread."""

    TICK_HZ = 30.0

    def __init__(self, rows: int = 10, cols: int = 12, seed: int = 42) -> None:
        self.rows = rows
        self.cols = cols
        self.seed = seed
        self.lock = threading.Lock()
        self.sim: Optional[Sim] = None
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._last_error: Optional[str] = None

    def start(self) -> None:
        try:
            self.sim = Sim(self.rows, self.cols, self.seed)
        except DispatchError as exc:
            self._last_error = str(exc)
            print(f"[server] Sim not available: {exc}", file=sys.stderr)
            return
        self._thread = threading.Thread(target=self._loop, name="sim-ticker", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.0)
        with self.lock:
            if self.sim is not None:
                self.sim.close()
                self.sim = None

    @property
    def available(self) -> bool:
        return self.sim is not None

    @property
    def last_error(self) -> Optional[str]:
        return self._last_error

    # 30 HZ TICKER LOOP: every ~33 ms, take the lock and call into the C ABI's
    # sim_tick(dt). Catching up via `next_t` keeps cadence steady even if a
    # tick runs long.
    def _loop(self) -> None:
        dt = 1.0 / self.TICK_HZ
        next_t = time.monotonic()
        while not self._stop.is_set():
            with self.lock:
                if self.sim is None:
                    return
                try:
                    self.sim.tick(dt)
                except Exception as exc:  # pragma: no cover — defensive
                    print(f"[server] tick failed: {exc}", file=sys.stderr)
                    return
            next_t += dt
            delay = next_t - time.monotonic()
            if delay > 0:
                self._stop.wait(delay)
            else:
                next_t = time.monotonic()


RUNNER = SimRunner(
    rows=int(os.environ.get("SIM_ROWS", 10)),
    cols=int(os.environ.get("SIM_COLS", 12)),
    seed=int(os.environ.get("SIM_SEED", 42)),
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _require_sim():
    if not RUNNER.available:
        return jsonify({
            "error": "sim unavailable",
            "detail": RUNNER.last_error or "DLL not loaded",
        }), 503
    return None


# SNAPSHOT BUILDER: copies the C-side view structs (nodes, roads, units,
# incidents, metrics) into plain Python dicts ready for JSON serialization.
# Crossing the FFI boundary once per /state poll keeps the wire format simple.
def _snapshot() -> Dict[str, Any]:
    sim = RUNNER.sim
    assert sim is not None
    metrics = sim.metrics()
    log_tail = sim.log_tail(4096)
    return {
        "time": metrics.sim_time,
        "paused": sim.is_paused(),
        "grid": {"rows": sim.rows, "cols": sim.cols},
        "nodes": [
            {"id": n.id, "row": n.row, "col": n.col,
             "x": n.x, "y": n.y, "street": n.street}
            for n in sim.nodes()
        ],
        "roads": [
            {"id": r.id, "from": r.from_node, "to": r.to_node,
             "blocked": r.blocked, "length": r.length}
            for r in sim.roads()
        ],
        "stations": [
            {"id": s.id, "type": int(s.type), "x": s.x, "y": s.y,
             "node": s.node, "n_units": s.n_units, "name": s.name}
            for s in sim.stations()
        ],
        "units": [
            {"id": u.id, "type": int(u.type), "state": int(u.state),
             "x": u.x, "y": u.y, "incident": u.incident_id, "eta": u.eta,
             "station": u.station_id, "cur_node": u.cur_node,
             "target_node": u.target_node}
            for u in sim.units()
        ],
        "incidents": [
            {"id": i.id, "type": int(i.type), "severity": int(i.severity),
             "x": i.x, "y": i.y, "assigned": i.assigned_unit,
             "resolved": 1 if i.resolved else 0, "spawn": i.spawn_time,
             "response_time": i.response_time}
            for i in sim.incidents()
        ],
        "metrics": {
            "total": metrics.total_incidents,
            "resolved": metrics.resolved_incidents,
            "pending": metrics.pending_incidents,
            "active_units": metrics.active_units,
            "idle_units": metrics.idle_units,
            "avg_resp": metrics.avg_response_time,
            "road_components": metrics.road_components,
            "pq_ops": metrics.pq_operations,
            "huffman_ratio": metrics.huffman_ratio,
            "dispatch_calls": metrics.dispatch_calls,
            "log_bytes": metrics.log_bytes,
            "log_bytes_huffman": metrics.log_bytes_huffman,
            "sa_suffixes": metrics.sa_suffix_count,
        },
        "log_tail": log_tail,
    }


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    return send_from_directory(str(STATIC_DIR), "index.html")


# /STATE ENDPOINT: returns the latest snapshot as JSON for the browser to
# render the map, leaderboards and metrics panels. Lock-protected so the
# background ticker can't mutate the C state mid-snapshot.
@app.route("/state")
def state():
    err = _require_sim()
    if err is not None:
        return err
    with RUNNER.lock:
        return jsonify(_snapshot())


@app.route("/tick", methods=["POST"])
def tick():
    err = _require_sim()
    if err is not None:
        return err
    data = request.get_json(silent=True) or {}
    dt = float(data.get("dt", 0.1))
    with RUNNER.lock:
        RUNNER.sim.tick(dt)  # type: ignore[union-attr]
        return jsonify(_snapshot())


@app.route("/control", methods=["POST"])
def control():
    err = _require_sim()
    if err is not None:
        return err
    data = request.get_json(silent=True) or {}
    with RUNNER.lock:
        sim = RUNNER.sim
        assert sim is not None
        if "paused" in data:
            sim.set_paused(bool(data["paused"]))
        if "spawn_rate" in data:
            sim.set_spawn_rate(float(data["spawn_rate"]))
        spawned_id = None
        if data.get("force_spawn"):
            spawned_id = sim.force_spawn()
        snap = _snapshot()
    if spawned_id is not None:
        snap["spawned_id"] = spawned_id
    return jsonify(snap)


@app.route("/search")
def search():
    err = _require_sim()
    if err is not None:
        return err
    q = request.args.get("q", "")
    fuzzy = request.args.get("fuzzy", "0") not in ("0", "", "false", "False")
    max_n = int(request.args.get("max", 10))
    with RUNNER.lock:
        sim = RUNNER.sim
        assert sim is not None
        if not q:
            matches = []
        elif fuzzy:
            max_edits = int(request.args.get("max_edits", 2))
            matches = sim.fuzzy(q, max_edits=max_edits, max=max_n)
        else:
            matches = sim.autocomplete(q, max=max_n)
    return jsonify({"q": q, "fuzzy": int(fuzzy), "matches": matches})


@app.route("/log/count")
def log_count():
    err = _require_sim()
    if err is not None:
        return err
    pattern = request.args.get("pattern", "")
    if not pattern:
        return jsonify({"count": 0, "pattern": ""})
    with RUNNER.lock:
        count = RUNNER.sim.log_count(pattern)  # type: ignore[union-attr]
    return jsonify({"count": count, "pattern": pattern})


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def _banner(host: str, port: int) -> None:
    dll_status = "loaded" if RUNNER.available else f"UNAVAILABLE ({RUNNER.last_error})"
    print("=" * 60)
    print("  Emergency Dispatch Simulator — Web UI")
    print("=" * 60)
    print(f"  libdispatch: {dll_status}")
    print(f"  Open http://{host}:{port}/  (Ctrl-C to stop)")
    print("=" * 60, flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=5000, type=int)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    RUNNER.start()
    try:
        _banner(args.host, args.port)
        # use_reloader=False so the background ticker isn't started twice.
        app.run(host=args.host, port=args.port, debug=args.debug, use_reloader=False)
    finally:
        RUNNER.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
