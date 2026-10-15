#!/usr/bin/env bash
# Part B experiment runner (B13): run one selection algorithm against four
# backends where backend #1 is deliberately slow (server_threads=1 vs 4).
#
# The unmodified Part A load client drives a fixed workload through the LB.
# On shutdown the LB reports the number of requests forwarded to each
# backend; that is the per-backend distribution we want (B13).
#
# Usage:
#   ./tests/lb/run_b13.sh <algo> <out_dir> [--requests N]
#     <algo>      probe | inflight | roundrobin
#     <out_dir>   where to store the LB summary + health log
set -euo pipefail

ALGO="${1:?usage: run_b13.sh <algo> <out_dir> [--requests N]}"
OUT_DIR="${2:?usage: run_b13.sh <algo> <out_dir> [--requests N]}"
REQUESTS=1000
if [[ "${3:-}" == "--requests" ]]; then REQUESTS="${4:?missing N}"; fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

mkdir -p "$OUT_DIR"

# Clean slate: no leftover processes or state.
pkill -x -INT load_balancer 2>/dev/null || true
pkill -x -INT server 2>/dev/null || true
sleep 1
rm -rf filestore backend_configs

# Start four backends, backend #1 (port 9001) slow (1 thread) (B13).
./scripts/start_backends.sh --sched rr --quantum 4096 \
    --slow-backend 1 --file-dir filestore --metrics-dir "$OUT_DIR" >/dev/null 2>&1
for p in 9001 9002 9003 9004; do
  for i in $(seq 1 20); do printf 'HEALTH\n' | nc -w 1 127.0.0.1 $p | grep -q 'OK' && break; sleep 0.5; done
done

# Start the LB with the requested algorithm.
nohup ./build/load_balancer --algo "$ALGO" --config config.json \
    --health-log "$OUT_DIR/health.csv" > "$OUT_DIR/lb.log" 2>&1 &
LBPID=$!
echo "$LBPID" > "$OUT_DIR/lb.pid"
sleep 2  # let the first probe cycle establish health state

# Drive the load through the LB with the unmodified Part A client (B2).
./build/client load workload --requests "$REQUESTS" --config backend_configs/config_lb.json >/dev/null 2>&1

# Gracefully stop the LB; it prints the per-backend forwarded counts (B5).
kill -INT "$LBPID"
sleep 1
cp "$OUT_DIR/lb.log" "$OUT_DIR/lb_summary.txt" 2>/dev/null || true

# Stop the backends gracefully (writes their metrics CSVs).
./scripts/stop_backends.sh --metrics-dir "$OUT_DIR" >/dev/null 2>&1
sleep 1

echo "=== B13 results for algo=$ALGO ==="
grep -E "Algorithm|backend_90|Total" "$OUT_DIR/lb_summary.txt"
echo "artifacts in $OUT_DIR"
