#!/usr/bin/env bash
# Part B experiment runner (B14): fault injection.
#
# In a single run, with the LB running and load in flight:
#   1. start four backends (all 4 threads) + the LB (inflight algorithm),
#   2. start the load client (closed-loop, load in flight),
#   3. kill -9 one backend (9001) after a short delay,
#   4. let the load continue while the health prober detects the failure
#      (within one health interval) and traffic is diverted,
#   5. restart the killed backend and let the prober restore it (B10),
#   6. let the load finish, stop the LB, and report:
#        - detection time (kill -> first 'down' probe) vs the health interval,
#        - number of failed requests (B6: ERR or short read) and why,
#        - re-rotation time (restart -> first 'healthy' probe).
#
# Usage: ./tests/lb/run_b14.sh <out_dir> [--requests N] [--algo inflight]
set -euo pipefail

OUT_DIR="${1:?usage: run_b14.sh <out_dir> [--requests N] [--algo inflight]}"
REQUESTS=5000
ALGO="inflight"
if [[ "${2:-}" == "--requests" ]]; then REQUESTS="${3:?missing N}"; fi
if [[ "${4:-}" == "--algo" ]]; then ALGO="${5:?missing algo}"; fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
mkdir -p "$OUT_DIR"

now_ms() { python3 -c 'import time; print(int(time.time()*1000))'; }

pkill -x -INT load_balancer 2>/dev/null || true
pkill -x -INT server 2>/dev/null || true
sleep 1
rm -rf filestore backend_configs

# Start four backends (all 4 threads) so the fault is isolated to one.
./scripts/start_backends.sh --sched rr --quantum 4096 \
    --file-dir filestore --metrics-dir "$OUT_DIR" >/dev/null 2>&1
for p in 9001 9002 9003 9004; do
  for i in $(seq 1 20); do printf 'HEALTH\n' | nc -w 1 127.0.0.1 $p | grep -q 'OK' && break; sleep 0.5; done
done

# Start the LB.
nohup ./build/load_balancer --algo "$ALGO" --config config.json \
    --health-log "$OUT_DIR/health.csv" > "$OUT_DIR/lb.log" 2>&1 &
LBPID=$!
echo "$LBPID" > "$OUT_DIR/lb.pid"
sleep 2

# Start the load in the background (load in flight).
./build/client load workload --requests "$REQUESTS" \
    --config backend_configs/config_lb.json > "$OUT_DIR/client.log" 2>&1 &
CLIENT_PID=$!

# Let load build up, then kill one backend (B14).
sleep 4
KILL_MS=$(now_ms)
VICTIM_PID=$(head -1 "$OUT_DIR/backend_pids")   # 9001
echo "$KILL_MS" > "$OUT_DIR/kill_ms"
echo "killing backend 9001 (pid $VICTIM_PID) at $KILL_MS"
kill -9 "$VICTIM_PID"

# Let the prober detect it and traffic divert, then restart the backend.
sleep 4
RESTART_MS=$(now_ms)
echo "$RESTART_MS" > "$OUT_DIR/restart_ms"
nohup ./build/server --sched rr --quantum 4096 --file filestore \
    --config backend_configs/backend_9001.json \
    --metrics-out "$OUT_DIR/metrics_9001.csv" \
    > "$OUT_DIR/backend_9001.log" 2>&1 &
echo $! >> "$OUT_DIR/backend_pids"
echo "restarted backend 9001 at $RESTART_MS"

# Let it re-enter rotation and the load finish.
sleep 4
wait "$CLIENT_PID" 2>/dev/null || true
sleep 1

# Gracefully stop the LB; it prints per-backend counts + total failed (B5).
kill -INT "$LBPID"
sleep 1
cp "$OUT_DIR/lb.log" "$OUT_DIR/lb_summary.txt" 2>/dev/null || true

./scripts/stop_backends.sh --metrics-dir "$OUT_DIR" >/dev/null 2>&1
sleep 1

echo "=== B14 results (algo=$ALGO) ==="
grep -E "Algorithm|backend_90|Total" "$OUT_DIR/lb_summary.txt"
echo "artifacts in $OUT_DIR"
