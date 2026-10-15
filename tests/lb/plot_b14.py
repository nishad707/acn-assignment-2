#!/usr/bin/env python3
"""
Part B (B14): plot the killed backend's health state over time, marking the
kill and restart instants, from a run directory (run_b14.sh output).

Usage:
  python3 tests/lb/plot_b14.py <run_dir> --out-plot plots/b14_health_timeline.png
"""
import argparse
import csv

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--out-plot", default="plots/b14_health_timeline.png")
    args = ap.parse_args()
    d = args.run_dir

    kill = int(open(f"{d}/kill_ms").read().strip())
    restart = int(open(f"{d}/restart_ms").read().strip())

    rows = [r for r in csv.DictReader(open(f"{d}/health.csv"))
            if r["backend"] == "backend_9001"]
    rows.sort(key=lambda r: int(r["timestamp"]))
    t = np.array([int(r["timestamp"]) - kill for r in rows]) / 1000.0
    s = np.array([1 if r["status"] == "healthy" else 0 for r in rows])

    fig, ax = plt.subplots(figsize=(9, 4))
    ax.step(t, s, where="post", color="tab:red", lw=2)
    ax.fill_between(t, s, step="post", color="tab:red", alpha=0.15)
    ax.axvline(0, color="black", ls="--", lw=1)
    ax.text(0, -0.18, "kill -9", ha="center", fontsize=9)
    ax.axvline((restart - kill) / 1000.0, color="green", ls="--", lw=1)
    ax.text((restart - kill) / 1000.0, 1.08, "restart", ha="center",
            fontsize=9, color="green")
    ax.set_ylim(-0.25, 1.25)
    ax.set_yticks([0, 1]); ax.set_yticklabels(["down", "healthy"])
    ax.set_xlabel("time relative to kill (s)")
    ax.set_title("B14: backend_9001 health state\n"
                 "(health interval 1000 ms)")
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(args.out_plot, dpi=120)
    print(f"wrote {args.out_plot}")


if __name__ == "__main__":
    main()
