#!/usr/bin/env bash
# Build libdispatch.dll if missing, then launch the Flask web UI.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

cd "$ROOT"

if [ ! -f "build/libdispatch.dll" ] && [ ! -f "build/libdispatch.so" ] && [ ! -f "build/libdispatch.dylib" ]; then
  echo "[run.sh] building libdispatch (make shared)..."
  make shared
fi

echo "[run.sh] starting Flask server on http://127.0.0.1:5000/"
exec python web/server.py "$@"
