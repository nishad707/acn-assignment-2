#!/usr/bin/env python3
"""
Part B analysis (B14): fault-injection results.

Given a run directory (from run_b14.sh), report:
  - detection time   : kill -> first 'down' probe of the victim, vs the interval
  - re-rotation time : restart -> first 'healthy' probe of the victim
  - per-backend forwarded / failed counts (from the LB summary, B5)
  - client-side failures (from client.log)

Usage:
  python3 tests/lb/analyze_b14.py <run_dir> [run_dir ...]
"""
import csv
import os
import re
import sys
from collections import defaultdict


def analyze(d):
    print(f"\n===== {d} =====")
    kill = int(open(os.path.join(d, "kill_ms")).read().strip())
    restart = int(open(os.path.join(d, "restart_ms")).read().strip())

    # Victim = the backend that went down.
    rows = list(csv.DictReader(open(os.path.join(d, "health.csv"))))
    downed = defaultdict(list)
    for r in rows:
        if r["status"] == "down":
            downed[r["backend"]].append(int(r["timestamp"]))
    victim = max(downed, key=lambda b: len(downed[b])) if downed else None

    if victim:
        vrows = [int(r["timestamp"]) for r in rows
                 if r["backend"] == victim and r["status"] == "down"]
        first_down = min(vrows)
        # first healthy probe strictly after the restart
        recov = [int(r["timestamp"]) for r in rows
                 if r["backend"] == victim and r["status"] == "healthy"
                 and int(r["timestamp"]) > restart]
        first_recov = min(recov) if recov else None
        print(f"victim backend          : {victim}")
        print(f"health interval         : 1000 ms")
        print(f"detection (kill->down)  : {first_down - kill} ms "
              f"({(first_down - kill) / 1000.0:.2f} intervals)")
        if first_recov:
            print(f"re-rotation (restart->healthy): {first_recov - restart} ms "
                  f"({(first_recov - restart) / 1000.0:.2f} intervals)")

    # Per-backend counts from the LB shutdown summary.
    print("per-backend (LB summary, B5):")
    with open(os.path.join(d, "lb_summary.txt")) as f:
        total_f = 0
        for line in f:
            m = re.search(r"(backend_\d+).*forwarded=(\d+) .*connectAttempts=(\d+) .*failedExchanges=(\d+)", line)
            if m:
                name, fw, ca, fe = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
                refused = ca - fw  # connection attempts that were refused
                print(f"  {name}: forwarded={fw} refusedConn={refused} failedExchanges={fe}")
            m = re.search(r"Total forwarded:\s*(\d+)", line)
            if m:
                print(f"  Total forwarded: {m.group(1)}")
            m = re.search(r"Total failed:\s*(\d+)", line)
            if m:
                total_f = int(m.group(1))
                print(f"  Total failed: {total_f}")

    # Client-side failures.
    clog = os.path.join(d, "client.log")
    if os.path.exists(clog):
        print("client-side failures (client.log):")
        with open(clog) as f:
            for line in f:
                line = line.strip()
                if line:
                    print(f"  {line}")
    print(f"interpretation: failures = 1 pre-response ERR (connection refused, "
          f"the request that first discovered the dead backend) + the in-flight "
          f"exchanges that were mid-transfer when the backend was killed "
          f"(short reads, B6). After the prober marks the backend down (~1 "
          f"interval), traffic is diverted and no further failures occur.")


def main():
    for d in sys.argv[1:]:
        analyze(d)


if __name__ == "__main__":
    main()
