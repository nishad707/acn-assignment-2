#!/usr/bin/env bash
# Part B helper (B3): start the four Part A backend servers that the load
# balancer fronts.
#
# Each backend is an independent instance of the Part A server on its own
# port, started with its own generated config (backend_configs/backend_<port>.json).
#
# Design decision (documented in README): all four backends share ONE --file
# directory (filestore/). Because the LB is transparent with no sticky
# sessions, a GET can land on any backend, so every backend must be able to
# serve every file. Seeding PUTs from the load client therefore populate the
# shared directory once and all backends see the files.
#
# Usage:
#   ./scripts/start_backends.sh [--sched rr] [--quantum 4096] \
#       [--slow-backend 1] [--file-dir filestore] [--metrics-dir lb_metrics]
#
# PIDs are written to <metrics-dir>/backend_pids so stop_backends.sh can stop
# them gracefully (SIGINT) so each writes its metrics CSV (A23).
set -euo pipefail

SCHED="rr"
QUANTUM="4096"
SLOW_BACKEND=""
FILE_DIR="filestore"
METRICS_DIR="lb_metrics"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sched)        SCHED="$2"; shift 2 ;;
        --quantum)      QUANTUM="$2"; shift 2 ;;
        --slow-backend) SLOW_BACKEND="$2"; shift 2 ;;
        --file-dir)     FILE_DIR="$2"; shift 2 ;;
        --metrics-dir)  METRICS_DIR="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# (Re)generate backend configs, making the chosen backend slow if requested.
python3 scripts/gen_backend_configs.py \
    --main-config config.json \
    --out-dir backend_configs \
    ${SLOW_BACKEND:+--slow-backend "$SLOW_BACKEND"}

# Fresh shared file store per experiment run.
rm -rf "$FILE_DIR"
mkdir -p "$FILE_DIR"
mkdir -p "$METRICS_DIR"

PIDS_FILE="$METRICS_DIR/backend_pids"
: > "$PIDS_FILE"

# Build the server argument vector once (rr/drr need --quantum; fcfs/sjf don't).
if [[ "$SCHED" == "rr" || "$SCHED" == "drr" ]]; then
    SCHED_ARGS=(--sched "$SCHED" --quantum "$QUANTUM")
else
    SCHED_ARGS=(--sched "$SCHED")
fi

for cfg in backend_configs/backend_*.json; do
    port="$(python3 -c "import json,sys; print(json.load(open('$cfg'))['server']['port'])")"
    log="$METRICS_DIR/backend_${port}.log"
    # nohup + full fd redirection so the launcher returns immediately and the
    # backends outlive this script.
    nohup ./build/server "${SCHED_ARGS[@]}" \
        --file "$FILE_DIR" \
        --config "$cfg" \
        --metrics-out "$METRICS_DIR/metrics_${port}.csv" \
        > "$log" 2>&1 &
    echo $! >> "$PIDS_FILE"
    echo "started backend on port $port (pid $(tail -1 "$PIDS_FILE"))"
done

echo "All four backends starting under --sched $SCHED${SLOW_BACKEND:+ (slow backend #$SLOW_BACKEND)}"
