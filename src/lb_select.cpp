/**
 * Part B: backend selection (B.3, B11/B12).
 *
 * Three algorithms, selectable at startup via --algo (B11). The two required
 * algorithms differ in the *kind of state* they read (B12):
 *
 *   "probe"    — probe-cadence state: refreshed once per health interval.
 *                Reads each healthy backend's last reported scheduler queue
 *                depth (B8) and picks a minimum. Because the signal is only as
 *                fresh as the last probe, this is what the B13 experiment
 *                probes: the behaviour *between* probes.
 *
 *   "inflight" — real-time state observed at the LB itself: updated on every
 *                dispatch and completion. Reads each healthy backend's count
 *                of requests currently in flight and picks a minimum.
 *
 *   "roundrobin" — stateless baseline (allowed as a third algorithm, B12).
 *
 * Selection is a single pass in round-robin order: it tracks the minimum
 * metric seen and, on a tie, keeps the backend nearest the (advancing) cursor.
 * As the cursor advances, the "nearest minimum" rotates, which spreads ties
 * fairly (no backend is structurally starved) without a second pass. A single
 * pass also avoids a TOCTOU race: a two-phase "compute min, then pick among
 * the minima" can return -1 when the metric (in-flight) changes between the
 * two phases even though healthy backends exist. Here, as long as at least one
 * healthy backend exists, a valid one is always returned. Selection is
 * lock-free: the state lives in std::atomic members (B4).
 */
#include "load_balancer.hpp"

#include <climits>

/**
 * Single-pass "least-X" selection with a round-robin tie-break.
 *
 * `metric` returns the (non-negative) value to minimise for a backend. The
 * pass scans in round-robin order from the shared cursor; a strictly smaller
 * value replaces the current best, so among equal minima the one closest to
 * the cursor wins — and as the cursor advances, that rotates among the tied
 * backends. Returns -1 only if no healthy backend exists.
 */
template <typename Metric>
static int selectLeast(const Metric &metric) {
    const size_t n = lb::backends.size();
    if (n == 0) {
        return -1;
    }
    uint64_t start = lb::roundRobinCursor.fetch_add(1) % n;
    long long best = LLONG_MAX;
    int bestIdx = -1;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % n;
        const Backend &b = lb::backends[idx];
        if (!b.healthy.load()) {
            continue;
        }
        long long v = metric(b);
        if (v < best) {  // strict < keeps the closest-to-cursor backend on ties
            best = v;
            bestIdx = static_cast<int>(idx);
        }
    }
    return bestIdx;
}

int lbSelectBackend() {
    if (lb::options.algorithm == LB_ALGO_PROBE) {
        // Probe-cadence state: the minimum last reported queue depth (B8/B12).
        return selectLeast([](const Backend &b) {
            return b.lastQueueDepth.load();
        });
    }

    if (lb::options.algorithm == LB_ALGO_INFLIGHT) {
        // Real-time state: the minimum in-flight request count (B12).
        return selectLeast([](const Backend &b) {
            return b.inFlight.load();
        });
    }

    // Default: stateless round-robin baseline (B11/B12) — first healthy
    // backend from the cursor.
    const size_t n = lb::backends.size();
    if (n == 0) {
        return -1;
    }
    uint64_t start = lb::roundRobinCursor.fetch_add(1) % n;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % n;
        if (lb::backends[idx].healthy.load()) {
            return static_cast<int>(idx);
        }
    }
    return -1;  // no healthy backend available
}
