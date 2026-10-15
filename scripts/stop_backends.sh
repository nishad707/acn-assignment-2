#!/usr/bin/env bash
# Part B helper: stop the four backend servers gracefully (SIGINT), so each
# writes its per-request metrics CSV (A23) and prints its shutdown summary.
#
# Usage: ./scripts/stop_backends.sh [--metrics-dir lb_metrics]
set -euo pipefail

METRICS_DIR="lb_metrics"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --metrics-dir) METRICS_DIR="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PIDS_FILE="$METRICS_DIR/backend_pids"
if [[ ! -f "$PIDS_FILE" ]]; then
    echo "no pid file at $PIDS_FILE; nothing to stop"
    exit 0
fi

while read -r pid; do
    if kill -INT "$pid" 2>/dev/null; then
        echo "sent SIGINT to backend pid $pid"
    else
        echo "pid $pid not running"
    fi
done < "$PIDS_FILE"

# Give the backends a moment to drain and write their CSVs.
sleep 1
echo "backends stopped"
