#!/usr/bin/env python3
"""
Part B analysis (B13): parse each algorithm's LB shutdown summary
(per-backend forwarded counts, B5) and produce the per-backend distribution
table + a grouped bar chart.

Usage:
  python3 tests/lb/analyze_b13.py \
      --runs probe:part_b_results/b13_probe \
             inflight:part_b_results/b13_inflight \
             roundrobin:part_b_results/b13_rr \
      --out-plot plots/b13_distribution.png \
      --out-table part_b_results/b13_table.txt
"""
import argparse
import os
import re
import sys


def parse_summary(path):
    """Extract per-backend forwarded counts from an LB shutdown summary."""
    counts = {}
    total = 0
    with open(path) as f:
        for line in f:
            m = re.search(r"(backend_\d+).*forwarded=(\d+)", line)
            if m:
                counts[m.group(1)] = int(m.group(2))
            m = re.search(r"Total forwarded:\s*(\d+)", line)
            if m:
                total = int(m.group(1))
    return counts, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", nargs="+", required=True,
                    help="name:directory pairs, e.g. probe:part_b_results/b13_probe")
    ap.add_argument("--out-plot", default="plots/b13_distribution.png")
    ap.add_argument("--out-table", default="part_b_results/b13_table.txt")
    args = ap.parse_args()

    results = {}
    for spec in args.runs:
        name, d = spec.split(":", 1)
        counts, total = parse_summary(os.path.join(d, "lb_summary.txt"))
        results[name] = (counts, total)

    backends = sorted({b for c, _ in results.values() for b in c})

    # ---- table ----
    lines = []
    lines.append("B13 per-backend request distribution (1000 load requests + 4 seed)")
    lines.append("backend_9001 is the deliberately slow backend (server_threads=1 vs 4).")
    header = "backend".ljust(14) + "".join(n.rjust(12) for n in results)
    lines.append(header)
    for b in backends:
        row = b.ljust(14)
        for name, (counts, total) in results.items():
            frac = counts.get(b, 0) / total if total else 0
            row += f"{frac*100:10.1f}% ".rjust(12)
        lines.append(row)
    total_row = "TOTAL".ljust(14)
    for name, (counts, total) in results.items():
        total_row += f"{total:12d}".rjust(12)
    lines.append(total_row)
    table = "\n".join(lines)
    print(table)
    os.makedirs(os.path.dirname(args.out_table) or ".", exist_ok=True)
    with open(args.out_table, "w") as f:
        f.write(table + "\n")

    # ---- plot ----
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("matplotlib not available; skipping plot", file=sys.stderr)
        return 0

    x = np.arange(len(backends))
    width = 0.8 / len(results)
    fig, ax = plt.subplots(figsize=(9, 5))
    for i, (name, (counts, total)) in enumerate(results.items()):
        fracs = [counts.get(b, 0) / total * 100 if total else 0 for b in backends]
        offset = (i - (len(results) - 1) / 2) * width
        bars = ax.bar(x + offset, fracs, width, label=name)
        for r, bar in zip(fracs, bars):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.5,
                    f"{r:.1f}", ha="center", va="bottom", fontsize=8)
    ax.set_ylabel("Fraction of requests (%)")
    ax.set_title("B13: per-backend request distribution\n"
                 "(backend_9001 = slow, 1 server thread; others 4 threads)")
    ax.set_xticks(x)
    ax.set_xticklabels([b.replace("backend_", "90") for b in backends], rotation=0)
    ax.set_ylim(0, 45)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    os.makedirs(os.path.dirname(args.out_plot) or ".", exist_ok=True)
    fig.tight_layout()
    fig.savefig(args.out_plot, dpi=120)
    print(f"wrote plot {args.out_plot}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
