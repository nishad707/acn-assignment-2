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
 * Both "least-X" algorithms break ties with a round-robin scan among the
 * backends that share the minimum value, so no backend is structurally
 * starved by the tie-break (a deterministic lowest-index tie-break would
 * always let the lower-index backends win). Selection is lock-free: the state
 * lives in std::atomic members, so a handler can pick a backend while probers
 * refresh health concurrently (B4).
 */
#include "load_balancer.hpp"

#include <climits>

/**
 * Pick a healthy backend whose metric equals `minValue`, scanning forward
 * from the shared round-robin cursor so ties are spread fairly. Returns -1 if
 * no healthy backend has that (minimum) value.
 */
static int pickAmongMinima(bool (*metricIsMin)(const Backend &, long long),
                           long long minValue) {
    const size_t n = lb::backends.size();
    if (n == 0) {
        return -1;
    }
    uint64_t start = lb::roundRobinCursor.fetch_add(1) % n;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % n;
        const Backend &b = lb::backends[idx];
        if (b.healthy.load() && metricIsMin(b, minValue)) {
            return static_cast<int>(idx);
        }
    }
    return -1;
}

int lbSelectBackend() {
    const size_t n = lb::backends.size();
    if (n == 0) {
        return -1;
    }

    if (lb::options.algorithm == LB_ALGO_PROBE) {
        // Probe-cadence state: the minimum last reported queue depth (B8/B12).
        long long best = LLONG_MAX;
        for (size_t i = 0; i < n; i++) {
            const Backend &b = lb::backends[i];
            if (b.healthy.load()) {
                long long d = b.lastQueueDepth.load();
                if (d < best) best = d;
            }
        }
        if (best == LLONG_MAX) return -1;  // no healthy backend
        return pickAmongMinima([](const Backend &b, long long m) {
            return b.lastQueueDepth.load() == m;
        }, best);
    }

    if (lb::options.algorithm == LB_ALGO_INFLIGHT) {
        // Real-time state: the minimum in-flight request count (B12).
        long long best = LLONG_MAX;
        for (size_t i = 0; i < n; i++) {
            const Backend &b = lb::backends[i];
            if (b.healthy.load()) {
                long long f = b.inFlight.load();
                if (f < best) best = f;
            }
        }
        if (best == LLONG_MAX) return -1;  // no healthy backend
        return pickAmongMinima([](const Backend &b, long long m) {
            return b.inFlight.load() == m;
        }, best);
    }

    // Default: stateless round-robin baseline (B11/B12).
    uint64_t start = lb::roundRobinCursor.fetch_add(1) % n;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % n;
        if (lb::backends[idx].healthy.load()) {
            return static_cast<int>(idx);
        }
    }
    return -1;  // no healthy backend available
}
