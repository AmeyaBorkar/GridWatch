"""Pythonic ctypes wrapper around ``build/libdispatch.dll``.

The C ABI is declared in ``include/dispatch/sim.h`` — this module mirrors every
view struct, every enum, and every exported function, and exposes a small,
idiomatic :class:`Sim` class plus lightweight ``NamedTuple`` views.

The DLL is loaded lazily in ``Sim.__init__`` so this module can be imported on
machines where the library has not been built yet (e.g. during CI, or while the
sim sources are still under construction).
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    byref,
    c_char,
    c_char_p,
    c_double,
    c_int,
    c_size_t,
    c_uint64,
    c_void_p,
    create_string_buffer,
)
from enum import IntEnum
from pathlib import Path
from typing import List, NamedTuple, Optional

__all__ = [
    "UnitType",
    "IncidentType",
    "UnitState",
    "Severity",
    "NodeView",
    "RoadView",
    "StationView",
    "UnitView",
    "IncidentView",
    "Metrics",
    "Sim",
    "DispatchError",
    "default_dll_path",
]


# ---------------------------------------------------------------------------
# Enums (mirroring sim.h)
# ---------------------------------------------------------------------------
class UnitType(IntEnum):
    AMBULANCE = 0
    FIRE = 1
    POLICE = 2


class IncidentType(IntEnum):
    MEDICAL = 0
    FIRE = 1
    CRIME = 2


class UnitState(IntEnum):
    IDLE = 0
    ENROUTE = 1
    ONSCENE = 2
    RETURNING = 3


class Severity(IntEnum):
    LOW = 1
    MED = 2
    HIGH = 3


# ---------------------------------------------------------------------------
# ctypes Structures (binary layout must match sim.h exactly)
# ---------------------------------------------------------------------------
class _NodeView(Structure):
    _fields_ = [
        ("id", c_int),
        ("row", c_int),
        ("col", c_int),
        ("x", c_double),
        ("y", c_double),
        ("street", c_char * 48),
    ]


class _RoadView(Structure):
    _fields_ = [
        ("id", c_int),
        ("from_node", c_int),
        ("to_node", c_int),
        ("length", c_double),
        ("blocked", c_int),
    ]


class _StationView(Structure):
    _fields_ = [
        ("id", c_int),
        ("type", c_int),
        ("node", c_int),
        ("x", c_double),
        ("y", c_double),
        ("n_units", c_int),
        ("name", c_char * 48),
    ]


class _UnitView(Structure):
    _fields_ = [
        ("id", c_int),
        ("station_id", c_int),
        ("type", c_int),
        ("state", c_int),
        ("cur_node", c_int),
        ("target_node", c_int),
        ("x", c_double),
        ("y", c_double),
        ("incident_id", c_int),
        ("eta", c_double),
    ]


class _IncidentView(Structure):
    _fields_ = [
        ("id", c_int),
        ("type", c_int),
        ("severity", c_int),
        ("node", c_int),
        ("x", c_double),
        ("y", c_double),
        ("spawn_time", c_double),
        ("assigned_unit", c_int),
        ("resolved", c_int),
        ("response_time", c_double),
    ]


class _Metrics(Structure):
    _fields_ = [
        ("sim_time", c_double),
        ("total_incidents", c_size_t),
        ("resolved_incidents", c_size_t),
        ("pending_incidents", c_size_t),
        ("active_units", c_size_t),
        ("idle_units", c_size_t),
        ("avg_response_time", c_double),
        ("road_components", c_size_t),
        ("log_bytes", c_size_t),
        ("log_bytes_huffman", c_size_t),
        ("huffman_ratio", c_double),
        ("sa_suffix_count", c_size_t),
        ("dispatch_calls", c_size_t),
        ("pq_operations", c_size_t),
    ]


# ---------------------------------------------------------------------------
# Pythonic views returned to callers
# ---------------------------------------------------------------------------
class NodeView(NamedTuple):
    id: int
    row: int
    col: int
    x: float
    y: float
    street: str


class RoadView(NamedTuple):
    id: int
    from_node: int
    to_node: int
    length: float
    blocked: bool


class StationView(NamedTuple):
    id: int
    type: UnitType
    node: int
    x: float
    y: float
    n_units: int
    name: str


class UnitView(NamedTuple):
    id: int
    station_id: int
    type: UnitType
    state: UnitState
    cur_node: int
    target_node: int
    x: float
    y: float
    incident_id: int
    eta: float


class IncidentView(NamedTuple):
    id: int
    type: IncidentType
    severity: Severity
    node: int
    x: float
    y: float
    spawn_time: float
    assigned_unit: int
    resolved: bool
    response_time: float


class Metrics(NamedTuple):
    sim_time: float
    total_incidents: int
    resolved_incidents: int
    pending_incidents: int
    active_units: int
    idle_units: int
    avg_response_time: float
    road_components: int
    log_bytes: int
    log_bytes_huffman: int
    huffman_ratio: float
    sa_suffix_count: int
    dispatch_calls: int
    pq_operations: int


class DispatchError(RuntimeError):
    """Raised when the DLL cannot be loaded or a sim call fails."""


# ---------------------------------------------------------------------------
# DLL resolution
# ---------------------------------------------------------------------------
def _project_root() -> Path:
    # web/dispatch_py/__init__.py -> web/dispatch_py -> web -> <root>
    return Path(__file__).resolve().parent.parent.parent


def default_dll_path() -> Path:
    env = os.environ.get("LIBDISPATCH_DLL")
    if env:
        return Path(env)
    root = _project_root()
    if sys.platform.startswith("win"):
        return root / "build" / "libdispatch.dll"
    if sys.platform == "darwin":
        return root / "build" / "libdispatch.dylib"
    return root / "build" / "libdispatch.so"


_dll_cache: Optional[ctypes.CDLL] = None


def _load_dll(path: Optional[Path] = None) -> ctypes.CDLL:
    global _dll_cache
    if _dll_cache is not None and path is None:
        return _dll_cache
    dll_path = Path(path) if path else default_dll_path()
    if not dll_path.exists():
        raise DispatchError(
            f"libdispatch shared library not found at {dll_path!s}. "
            "Run `make shared` (or set $LIBDISPATCH_DLL)."
        )
    try:
        dll = ctypes.CDLL(str(dll_path))
    except OSError as exc:
        raise DispatchError(f"Failed to load {dll_path}: {exc}") from exc
    _bind(dll)
    if path is None:
        _dll_cache = dll
    return dll


def _bind(dll: ctypes.CDLL) -> None:
    """Attach argtypes/restype to every exported sim_* function."""
    # lifecycle
    dll.sim_create.argtypes = [c_int, c_int, c_uint64]
    dll.sim_create.restype = c_void_p
    dll.sim_destroy.argtypes = [c_void_p]
    dll.sim_destroy.restype = None
    dll.sim_tick.argtypes = [c_void_p, c_double]
    dll.sim_tick.restype = None
    dll.sim_set_paused.argtypes = [c_void_p, c_int]
    dll.sim_set_paused.restype = None
    dll.sim_is_paused.argtypes = [c_void_p]
    dll.sim_is_paused.restype = c_int
    dll.sim_set_spawn_rate.argtypes = [c_void_p, c_double]
    dll.sim_set_spawn_rate.restype = None
    dll.sim_force_spawn.argtypes = [c_void_p]
    dll.sim_force_spawn.restype = c_int
    # shape
    dll.sim_rows.argtypes = [c_void_p]
    dll.sim_rows.restype = c_int
    dll.sim_cols.argtypes = [c_void_p]
    dll.sim_cols.restype = c_int
    # snapshots
    dll.sim_nodes.argtypes = [c_void_p, POINTER(_NodeView), c_size_t]
    dll.sim_nodes.restype = c_size_t
    dll.sim_roads.argtypes = [c_void_p, POINTER(_RoadView), c_size_t]
    dll.sim_roads.restype = c_size_t
    dll.sim_stations.argtypes = [c_void_p, POINTER(_StationView), c_size_t]
    dll.sim_stations.restype = c_size_t
    dll.sim_units.argtypes = [c_void_p, POINTER(_UnitView), c_size_t]
    dll.sim_units.restype = c_size_t
    dll.sim_incidents.argtypes = [c_void_p, POINTER(_IncidentView), c_size_t]
    dll.sim_incidents.restype = c_size_t
    # search
    dll.sim_autocomplete.argtypes = [c_void_p, c_char_p, POINTER(c_char_p), c_size_t]
    dll.sim_autocomplete.restype = c_size_t
    dll.sim_fuzzy.argtypes = [c_void_p, c_char_p, c_int, POINTER(c_char_p), c_size_t]
    dll.sim_fuzzy.restype = c_size_t
    dll.sim_node_by_street.argtypes = [c_void_p, c_char_p]
    dll.sim_node_by_street.restype = c_int
    # metrics + log
    dll.sim_metrics.argtypes = [c_void_p, POINTER(_Metrics)]
    dll.sim_metrics.restype = None
    dll.sim_log_tail.argtypes = [c_void_p, c_char_p, c_size_t]
    dll.sim_log_tail.restype = c_size_t
    dll.sim_log_count.argtypes = [c_void_p, c_char_p]
    dll.sim_log_count.restype = c_size_t
    # Note: sim_autocomplete / sim_fuzzy return strings the caller is nominally
    # responsible for freeing, but the CRT used by the DLL may differ from
    # whatever ctypes.CDLL('msvcrt.dll') resolves to inside the Python process,
    # which corrupts the heap on free. We deliberately do NOT call free on
    # these strings here: a handful of small allocations per search are a fine
    # tradeoff against heap-corruption crashes. If the C side's allocator is
    # ever proven to match, flip ``dll._libc_free`` to a callable.
    dll._libc_free = None  # type: ignore[attr-defined]


# ---------------------------------------------------------------------------
# Main Sim class
# ---------------------------------------------------------------------------
class Sim:
    """Handle to a running simulation; thin wrapper over a C ``sim_t*``."""

    # caller-facing caps for snapshot buffers; generous but bounded
    MAX_NODES = 4096
    MAX_ROADS = 16384
    MAX_STATIONS = 256
    MAX_UNITS = 512
    MAX_INCIDENTS = 4096
    MAX_MATCHES = 64
    LOG_TAIL_DEFAULT = 4096

    def __init__(
        self,
        rows: int = 10,
        cols: int = 12,
        seed: int = 42,
        *,
        dll_path: Optional[Path] = None,
    ) -> None:
        self._dll = _load_dll(dll_path)
        handle = self._dll.sim_create(int(rows), int(cols), c_uint64(int(seed)))
        if not handle:
            raise DispatchError("sim_create returned NULL")
        self._handle: Optional[int] = handle
        self.rows = int(self._dll.sim_rows(handle))
        self.cols = int(self._dll.sim_cols(handle))

    # -- context manager ----------------------------------------------------
    def __enter__(self) -> "Sim":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def close(self) -> None:
        if self._handle:
            self._dll.sim_destroy(self._handle)
            self._handle = None

    # -- helpers ------------------------------------------------------------
    def _h(self) -> int:
        if not self._handle:
            raise DispatchError("Sim has been closed")
        return self._handle

    # -- lifecycle / control ------------------------------------------------
    def tick(self, dt: float = 0.1) -> None:
        self._dll.sim_tick(self._h(), c_double(float(dt)))

    def set_paused(self, paused: bool) -> None:
        self._dll.sim_set_paused(self._h(), 1 if paused else 0)

    def is_paused(self) -> bool:
        return bool(self._dll.sim_is_paused(self._h()))

    def set_spawn_rate(self, per_second: float) -> None:
        self._dll.sim_set_spawn_rate(self._h(), c_double(float(per_second)))

    def force_spawn(self) -> int:
        return int(self._dll.sim_force_spawn(self._h()))

    # -- snapshots ----------------------------------------------------------
    def nodes(self) -> List[NodeView]:
        buf = (_NodeView * self.MAX_NODES)()
        n = self._dll.sim_nodes(self._h(), buf, self.MAX_NODES)
        return [
            NodeView(
                id=b.id, row=b.row, col=b.col, x=b.x, y=b.y,
                street=b.street.decode("utf-8", "replace"),
            )
            for b in buf[:n]
        ]

    def roads(self) -> List[RoadView]:
        buf = (_RoadView * self.MAX_ROADS)()
        n = self._dll.sim_roads(self._h(), buf, self.MAX_ROADS)
        return [
            RoadView(
                id=b.id, from_node=b.from_node, to_node=b.to_node,
                length=b.length, blocked=bool(b.blocked),
            )
            for b in buf[:n]
        ]

    def stations(self) -> List[StationView]:
        buf = (_StationView * self.MAX_STATIONS)()
        n = self._dll.sim_stations(self._h(), buf, self.MAX_STATIONS)
        return [
            StationView(
                id=b.id, type=UnitType(b.type), node=b.node,
                x=b.x, y=b.y, n_units=b.n_units,
                name=b.name.decode("utf-8", "replace"),
            )
            for b in buf[:n]
        ]

    def units(self) -> List[UnitView]:
        buf = (_UnitView * self.MAX_UNITS)()
        n = self._dll.sim_units(self._h(), buf, self.MAX_UNITS)
        return [
            UnitView(
                id=b.id, station_id=b.station_id,
                type=UnitType(b.type), state=UnitState(b.state),
                cur_node=b.cur_node, target_node=b.target_node,
                x=b.x, y=b.y, incident_id=b.incident_id, eta=b.eta,
            )
            for b in buf[:n]
        ]

    def incidents(self) -> List[IncidentView]:
        buf = (_IncidentView * self.MAX_INCIDENTS)()
        n = self._dll.sim_incidents(self._h(), buf, self.MAX_INCIDENTS)
        return [
            IncidentView(
                id=b.id, type=IncidentType(b.type),
                severity=Severity(b.severity) if b.severity in (1, 2, 3) else Severity.LOW,
                node=b.node, x=b.x, y=b.y, spawn_time=b.spawn_time,
                assigned_unit=b.assigned_unit, resolved=bool(b.resolved),
                response_time=b.response_time,
            )
            for b in buf[:n]
        ]

    # -- search -------------------------------------------------------------
    def _collect_strings(self, fn, *args, max_items: int) -> List[str]:
        buf = (c_char_p * max_items)()
        n = int(fn(*args, buf, max_items))
        out: List[str] = []
        for i in range(n):
            p = buf[i]
            if p:
                try:
                    out.append(p.decode("utf-8", "replace"))
                finally:
                    free = getattr(self._dll, "_libc_free", None)
                    if free is not None:
                        free(p)
        return out

    def autocomplete(self, prefix: str, max: int = 10) -> List[str]:
        max_items = min(int(max), self.MAX_MATCHES)
        return self._collect_strings(
            self._dll.sim_autocomplete, self._h(), prefix.encode("utf-8"),
            max_items=max_items,
        )

    def fuzzy(self, query: str, max_edits: int = 2, max: int = 10) -> List[str]:
        max_items = min(int(max), self.MAX_MATCHES)
        return self._collect_strings(
            self._dll.sim_fuzzy, self._h(), query.encode("utf-8"), int(max_edits),
            max_items=max_items,
        )

    def node_by_street(self, name: str) -> int:
        return int(self._dll.sim_node_by_street(self._h(), name.encode("utf-8")))

    # -- metrics + log ------------------------------------------------------
    def metrics(self) -> Metrics:
        m = _Metrics()
        self._dll.sim_metrics(self._h(), byref(m))
        return Metrics(
            sim_time=m.sim_time,
            total_incidents=m.total_incidents,
            resolved_incidents=m.resolved_incidents,
            pending_incidents=m.pending_incidents,
            active_units=m.active_units,
            idle_units=m.idle_units,
            avg_response_time=m.avg_response_time,
            road_components=m.road_components,
            log_bytes=m.log_bytes,
            log_bytes_huffman=m.log_bytes_huffman,
            huffman_ratio=m.huffman_ratio,
            sa_suffix_count=m.sa_suffix_count,
            dispatch_calls=m.dispatch_calls,
            pq_operations=m.pq_operations,
        )

    def log_tail(self, cap: int = LOG_TAIL_DEFAULT) -> str:
        cap = max(1, int(cap))
        buf = create_string_buffer(cap + 1)
        n = int(self._dll.sim_log_tail(self._h(), buf, cap))
        return buf.raw[:n].decode("utf-8", "replace")

    def log_count(self, pattern: str) -> int:
        return int(self._dll.sim_log_count(self._h(), pattern.encode("utf-8")))
