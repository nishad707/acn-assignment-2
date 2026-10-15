/**
 * Part B: health checking (B.2, B7-B10).
 *
 * One prober thread is started per backend, so the four probes run fully
 * independently (B7): a slow or hung probe of one backend can never delay the
 * probes of the others.
 *
 * Each cycle a prober:
 *   1. connects to the backend (timeout = one health interval),
 *   2. sends "HEALTH\n" and reads the reply (timeout = one health interval),
 *   3. parses "OK <queue_depth>\n" (the backend's current scheduler queue
 *      depth, B8),
 *   4. appends one line to the health log (B9):
 *          timestamp,backend,status,rtt_ms,queue_depth
 *   5. marks the backend healthy (and records its queue depth / rtt) on
 *      success, or unhealthy on any failure (B10).
 *
 * A probe fails (status=down) on: a connection error, a malformed reply, or
 * the absence of a complete reply within one health interval (B7).
 *
 * Design choices (documented in README):
 *   - timestamp: epoch milliseconds (integer), for precise timing analysis.
 *   - queue_depth for a failed probe: -1 (no depth was reported).
 *   - rtt_ms: integer milliseconds measured on a monotonic clock.
 */
#include "load_balancer.hpp"

#include "lb_io.hpp"
#include "response.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace {

std::vector<pthread_t> proberThreads;
std::mutex proberMutex;
bool healthLogInitialized = false;

// Interruptible sleep so lbStopHealthProbers() can wake a sleeping prober.
std::mutex sleepMutex;
std::condition_variable sleepCv;

/** Wall-clock time in epoch milliseconds (for the log timestamp, B9). */
long long epochMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

/** Monotonic clock in nanoseconds (for measuring probe RTT, B7/B9). */
int64_t monoNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/**
 * Run a single probe against a backend.
 *
 * Returns true if the backend answered "OK <depth>" within the interval;
 * false on connection error, malformed reply, or timeout. On success, the
 * reported queue depth is written to `depthOut`.
 */
bool probeOne(const Backend &b, long long &depthOut) {
    depthOut = -1;
    int sock = lbConnectToAddress(b.ip, b.port, lb::healthIntervalMs);
    if (sock < 0) {
        return false;  // connection error (B7)
    }

    const std::string req = "HEALTH\n";
    if (!sendAllBytes(sock, req.data(), req.size())) {
        close(sock);
        return false;
    }

    BufferedSocket bs(sock);
    std::string line;
    // recv is bounded by the SO_RCVTIMEO set in lbConnectToAddress, so a
    // backend that never replies causes readLine to time out and fail (B7).
    if (!bs.readLine(line)) {
        close(sock);
        return false;  // no complete reply within one interval (B7)
    }
    close(sock);

    // Expect exactly "OK <digits>\n".
    if (line.rfind("OK ", 0) != 0) {
        return false;  // malformed reply (B7)
    }
    std::string num = line.substr(3);
    if (!num.empty() && num.back() == '\n') {
        num.pop_back();
    }
    if (num.empty()) {
        return false;
    }
    for (char c : num) {
        if (c < '0' || c > '9') {
            return false;  // malformed reply (B7)
        }
    }
    try {
        depthOut = static_cast<long long>(std::stoull(num));
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * Append one line to the shared health log under a lock (B9).
 * Columns: timestamp,backend,status,rtt_ms,queue_depth
 */
void logProbe(const std::string &path, const Backend &b, bool ok,
              long long rttMs, long long depth) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> guard(logMutex);

    // Write the header once, the first time the log is touched.
    if (!healthLogInitialized) {
        std::FILE *f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "timestamp,backend,status,rtt_ms,queue_depth\n");
            std::fclose(f);
            healthLogInitialized = true;
        }
    }

    std::FILE *f = std::fopen(path.c_str(), "a");
    if (!f) {
        return;
    }
    std::fprintf(f, "%lld,%s,%s,%lld,%lld\n",
                 epochMs(),
                 b.name.c_str(),
                 ok ? "healthy" : "down",
                 rttMs,
                 depth);
    std::fclose(f);
}

/** Prober thread body: probe this backend, log, update state, sleep. */
void *proberThreadFunc(void *arg) {
    int idx = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    Backend &b = lb::backends[idx];

    while (!lb::stopRequested.load()) {
        int64_t t0 = monoNs();
        long long depth = -1;
        bool ok = probeOne(b, depth);
        int64_t t1 = monoNs();
        long long rttMs = (t1 - t0) / 1000000LL;

        // Record liveness + state for the selection algorithms (B10).
        if (ok) {
            b.healthy.store(true);
            b.lastQueueDepth.store(depth);
            b.lastRttMs.store(rttMs);
        } else {
            b.healthy.store(false);
        }

        logProbe(lb::options.healthLogPath, b, ok, rttMs, ok ? depth : -1);

        // Sleep for the remainder of the health interval (B7 cadence).
        long long sleepMs = static_cast<long long>(lb::healthIntervalMs) - rttMs;
        if (sleepMs > 0) {
            std::unique_lock<std::mutex> lk(sleepMutex);
            sleepCv.wait_for(lk, std::chrono::milliseconds(sleepMs),
                             [] { return lb::stopRequested.load(); });
        }
    }
    return nullptr;
}

}  // namespace

void lbStartHealthProbers() {
    for (size_t i = 0; i < lb::backends.size(); i++) {
        pthread_t t;
        int rc = pthread_create(&t, nullptr, proberThreadFunc,
                                reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        if (rc != 0) {
            std::cerr << "error: failed to create health prober thread" << std::endl;
            continue;
        }
        proberThreads.push_back(t);
    }
    std::cout << "[LB] started " << proberThreads.size()
              << " health probers (interval=" << lb::healthIntervalMs << "ms)"
              << std::endl;
}

void lbStopHealthProbers() {
    {
        std::unique_lock<std::mutex> lk(sleepMutex);
        sleepCv.notify_all();  // wake sleeping probers so they see stopRequested
    }
    for (pthread_t t : proberThreads) {
        pthread_join(t, nullptr);
    }
    proberThreads.clear();
    std::cout << "[LB] health probers stopped" << std::endl;
}

void lbLogHealthHeaderIfNeeded(const std::string &path) {
    // Header is written lazily by logProbe; this hook is kept for clarity
    // and possible pre-creation of the log file.
    (void)path;
}
