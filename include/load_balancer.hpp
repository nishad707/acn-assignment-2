/**
 * Part B: Load Balancer — shared state and component interfaces.
 *
 * The load balancer (LB) sits in front of four independent Part A server
 * instances ("backends"), each on its own port (B3). The LB:
 *   - accepts client connections and relays PUT/GET transparently (B1/B2),
 *   - picks a backend per request using a selectable algorithm (B11/B12),
 *   - probes every backend for liveness + scheduler queue depth (B7/B8),
 *   - logs each probe and reports per-backend forwarded counts at shutdown.
 *
 * Concurrency model (B4): the main thread only accepts; every client
 * connection is handed to its own handler thread, so concurrent clients never
 * serialize behind one another. One prober thread per backend keeps health
 * probes independent (B7: a slow probe must not delay the others).
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "config.hpp"
#include "cli.hpp"
#include "request.hpp"

/**
 * One backend server: an instance of the Part A server on a distinct port.
 *
 * The atomic members are shared between the health-prober threads (which
 * refresh liveness + queue depth) and the client-handler threads (which read
 * them to pick a backend and to update in-flight counters). Atomics are used
 * so a handler can make a selection without taking a global lock (B4).
 */
struct Backend {
    std::string ip;
    int port = 0;
    std::string name;  // "backend_<port>", used in logs and the shutdown report

    // ---- health state, refreshed once per health interval (B7/B8/B10) ----
    // healthy: whether this backend may currently be selected. A failed probe
    // (or a failed connection) marks it false; a successful probe restores it.
    std::atomic<bool> healthy{true};  // start optimistic; first probe corrects
    // lastQueueDepth: scheduler queue depth reported by the most recent
    // successful probe (B8). This is the "probe-cadence state" the "probe"
    // selection algorithm reads (B12).
    std::atomic<long long> lastQueueDepth{0};
    // lastRttMs: round-trip time of the most recent probe, in ms (B9). Kept
    // for the log/report; it is deliberately NOT used for selection because
    // HEALTH bypasses the queue, so RTT barely moves with load (B12).
    std::atomic<long long> lastRttMs{0};

    // ---- real-time state, updated on every dispatch/completion (B12) ----
    // inFlight: number of exchanges currently in progress with this backend.
    // Incremented when the LB commits a request to the backend and decremented
    // when the exchange finishes (success or failure). Read by "inflight".
    std::atomic<int> inFlight{0};

    // ---- statistics, reported at shutdown (B5) ----
    // forwarded: number of requests the LB committed to this backend.
    std::atomic<uint64_t> forwarded{0};
    // connectAttempts: every time the LB tried to open a connection here.
    std::atomic<uint64_t> connectAttempts{0};
    // failedExchanges: exchanges that started but did not complete cleanly
    // (backend went down mid-transfer, etc.).
    std::atomic<uint64_t> failedExchanges{0};

    // std::atomic members are non-copyable and non-movable, which would make
    // std::vector<Backend> unable to reallocate. Provide explicit copy and
    // move constructors that transfer the atomic values via load/store. The
    // vector pre-reserves capacity in main, so in practice these are only
    // needed to satisfy the compiler; if a move did occur it would still copy
    // the current values correctly.
    Backend() = default;
    Backend(const Backend &o) {
        ip = o.ip;
        port = o.port;
        name = o.name;
        healthy = o.healthy.load();
        lastQueueDepth = o.lastQueueDepth.load();
        lastRttMs = o.lastRttMs.load();
        inFlight = o.inFlight.load();
        forwarded = o.forwarded.load();
        connectAttempts = o.connectAttempts.load();
        failedExchanges = o.failedExchanges.load();
    }
    Backend(Backend &&o) : Backend(o) {}
    Backend &operator=(const Backend &) = delete;
    Backend &operator=(Backend &&) = delete;
};

/**
 * Global LB state shared across threads.
 */
namespace lb {
    // The four backends, in config order (B3: backends[] must hold exactly 4).
    extern std::vector<Backend> backends;
    // Parsed CLI options.
    extern LoadBalancerOptions options;
    // The health interval in ms, copied from config for convenience (B7).
    extern int healthIntervalMs;
    // Set by the SIGINT/SIGTERM handler to request a graceful shutdown (B5).
    extern std::atomic<bool> stopRequested;

    // Aggregate counters for the shutdown summary (B5).
    extern std::atomic<uint64_t> totalForwarded;
    extern std::atomic<uint64_t> totalFailed;

    // Shared round-robin cursor for the baseline algorithm (B11/B12).
    extern std::atomic<uint64_t> roundRobinCursor;

    // Client-handler bookkeeping used for the graceful-shutdown drain (B5):
    // wait until every in-flight client exchange has completed.
    extern std::mutex activeMutex;
    extern std::condition_variable activeCv;
    extern std::atomic<int> activeHandlers;
}

// ---- component entry points (defined in separate translation units) ----

// lb_select.cpp
// Pick a backend index to serve the next request, or -1 if none is healthy.
int lbSelectBackend();

// lb_health.cpp
void lbStartHealthProbers();
void lbStopHealthProbers();
void lbLogHealthHeaderIfNeeded(const std::string &path);

// lb_proxy.cpp
// Handle one client connection end-to-end (relay a single request).
void lbHandleClientConnection(int clientSocket);
