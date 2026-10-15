/**
 * Part B: backend selection (B11/B12).
 *
 * The LB must pick one of the (currently healthy) backends for each request.
 *
 * This file currently implements the stateless "roundrobin" baseline, which
 * cycles through the healthy backends in order. The two required algorithms
 * of B12 — "probe" (probe-cadence state: last reported queue depth) and
 * "inflight" (real-time state: requests in flight) — are added in section B3.
 *
 * Selection is done without a global lock: each Backend's health and counter
 * state lives in std::atomic members, so a handler thread can pick a backend
 * while prober threads refresh health concurrently (B4).
 */
#include "load_balancer.hpp"

#include <climits>
#include <cstdlib>

int lbSelectBackend() {
    const size_t n = lb::backends.size();
    if (n == 0) {
        return -1;
    }

    // Stateless baseline: round-robin over the healthy backends.
    // We take the current cursor as our starting point and scan forward until
    // we find a healthy backend. Because the cursor is a shared atomic,
    // concurrent handlers naturally spread across the backends.
    uint64_t start = lb::roundRobinCursor.fetch_add(1) % n;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % n;
        if (lb::backends[idx].healthy.load()) {
            return static_cast<int>(idx);
        }
    }
    return -1;  // no healthy backend available
}
