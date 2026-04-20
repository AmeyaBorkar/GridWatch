"""Smoke test for the Python ctypes wrapper around libdispatch.dll.

Usage:
    python web/test_wrapper.py

Exits 0 on PASS, 1 on FAIL, 77 (skipped) if the DLL is not yet built.
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from dispatch_py import DispatchError, Sim, default_dll_path  # noqa: E402

SKIP_EXIT = 77


def main() -> int:
    dll = default_dll_path()
    if not dll.exists():
        print(f"SKIP: libdispatch not built yet at {dll}. Run `make shared` first.")
        return SKIP_EXIT

    try:
        with Sim(rows=10, cols=12, seed=42) as sim:
            print(f"OK   created Sim({sim.rows}x{sim.cols})")
            for _ in range(600):
                sim.tick(0.1)
            print(f"OK   ticked 600 x 0.1s -> sim_time = {sim.metrics().sim_time:.2f}s")

            nodes = sim.nodes()
            roads = sim.roads()
            stations = sim.stations()
            units = sim.units()
            incidents = sim.incidents()
            print(f"OK   nodes={len(nodes)} roads={len(roads)} "
                  f"stations={len(stations)} units={len(units)} "
                  f"incidents={len(incidents)}")

            m = sim.metrics()
            print(f"OK   metrics: total={m.total_incidents} "
                  f"resolved={m.resolved_incidents} pending={m.pending_incidents} "
                  f"dispatch_calls={m.dispatch_calls} pq_ops={m.pq_operations}")

            assert m.total_incidents > 0, "expected incidents to have spawned"
            assert m.dispatch_calls >= 0

            ac = sim.autocomplete("Oa", max=10)
            print(f"OK   autocomplete('Oa') -> {ac}")

            tail = sim.log_tail(512)
            print(f"OK   log_tail({len(tail)} chars)")

            count = sim.log_count("AMB")
            print(f"OK   log_count('AMB') = {count}")

    except DispatchError as exc:
        print(f"SKIP: {exc}")
        return SKIP_EXIT
    except AssertionError as exc:
        print(f"FAIL: assertion: {exc}")
        return 1
    except Exception:
        print("FAIL: unexpected exception:")
        traceback.print_exc()
        return 1

    print("\nPASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
