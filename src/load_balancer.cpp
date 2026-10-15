/**
 * Part B: Load Balancer main program (B1/B3/B4/B5).
 *
 * Starts the listening socket on load_balancer.ip:port, spawns one handler
 * thread per accepted client connection (B4: concurrent clients never
 * serialize), and — in a later section (B2) — starts one health-prober
 * thread per backend. On SIGINT/SIGTERM it stops accepting, waits for the
 * in-flight exchanges to drain (B5), prints the per-backend forwarded
 * counts, and exits 0.
 */
#include "load_balancer.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

// poll() timeout (ms) so the accept loop can observe stopRequested promptly
// even when no client is connecting (B5: stop accepting on signal).
constexpr int ACCEPT_POLL_TIMEOUT_MS = 200;

void handleSignal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        lb::stopRequested.store(true);
    }
}

}  // namespace

// ---- global state defined here, declared in load_balancer.hpp ----
std::vector<Backend> lb::backends;
LoadBalancerOptions lb::options;
int lb::healthIntervalMs = 1000;
std::atomic<bool> lb::stopRequested{false};
std::atomic<uint64_t> lb::totalForwarded{0};
std::atomic<uint64_t> lb::totalFailed{0};
std::atomic<uint64_t> lb::roundRobinCursor{0};
std::mutex lb::activeMutex;
std::condition_variable lb::activeCv;
std::atomic<int> lb::activeHandlers{0};

int main(int argc, char *argv[]) {
    // Graceful shutdown on SIGINT/SIGTERM (B5), same discipline as A26.
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    if (!parseLoadBalancerArguments(argc, argv, lb::options)) {
        return 1;
    }

    Config config;
    try {
        config = loadConfig(lb::options.configPath);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }

    // Build the backend list from config (B3: exactly four backends).
    // Backend is non-movable (it holds atomics), so we reserve the exact
    // capacity up front and construct each entry in place.
    lb::healthIntervalMs = config.loadBalancer.healthIntervalMs;
    lb::backends.reserve(config.loadBalancer.backends.size());
    for (const BackendConfig &bc : config.loadBalancer.backends) {
        Backend &b = lb::backends.emplace_back();
        b.ip = bc.ipAddress;
        b.port = bc.port;
        b.name = "backend_" + std::to_string(bc.port);
    }

    if (lb::backends.empty()) {
        std::cerr << "error: no backends configured" << std::endl;
        return 1;
    }

    // Listening socket with SO_REUSEADDR so a restarted LB can rebind the
    // port immediately (mirrors A26; the B14 recovery test depends on it).
    int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket < 0) {
        std::cerr << "error: failed to create LB socket" << std::endl;
        return 1;
    }
    int reuse = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.loadBalancer.port));
    if (inet_pton(AF_INET, config.loadBalancer.ipAddress.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "error: invalid LB ip" << std::endl;
        close(listenSocket);
        return 1;
    }

    if (bind(listenSocket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "error: failed to bind LB socket on port "
                  << config.loadBalancer.port << std::endl;
        close(listenSocket);
        return 1;
    }
    if (listen(listenSocket, 64) < 0) {
        std::cerr << "error: failed to listen" << std::endl;
        close(listenSocket);
        return 1;
    }

    std::cout << "[LB] listening on " << config.loadBalancer.ipAddress << ":"
              << config.loadBalancer.port
              << " | algo=" << lb::options.algorithm
              << " | backends=" << lb::backends.size() << std::endl;

    // (B2) Start one independent health prober per backend.
    lbStartHealthProbers();

    // ---- accept loop ----
    while (!lb::stopRequested.load()) {
        // poll with a timeout so we can react to stopRequested between clients.
        pollfd pfd{listenSocket, POLLIN, 0};
        int pr = poll(&pfd, 1, ACCEPT_POLL_TIMEOUT_MS);
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::cerr << "error: poll failed" << std::endl;
            break;
        }
        if (pr == 0) {
            continue;  // timeout; loop back to re-check stopRequested
        }
        int clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket < 0) {
            continue;
        }
        // Hand the connection to its own thread; detach because the handler
        // tracks its own lifetime via activeHandlers for the shutdown drain.
        std::thread(lbHandleClientConnection, clientSocket).detach();
    }

    // ---- graceful shutdown (B5) ----
    close(listenSocket);  // stop accepting
    std::cout << "\n[LB] shutting down, draining in-flight exchanges..." << std::endl;

    {
        std::unique_lock<std::mutex> lock(lb::activeMutex);
        lb::activeCv.wait(lock, [] { return lb::activeHandlers.load() == 0; });
    }

    // (B2) Stop health probers once traffic has drained.
    lbStopHealthProbers();

    // ---- shutdown summary (B5): per-backend forwarded counts ----
    std::cout << "===== Load Balancer Shutdown Summary =====" << std::endl;
    std::cout << "Algorithm: " << lb::options.algorithm << std::endl;
    for (const Backend &b : lb::backends) {
        std::cout << "  " << b.name << " (" << b.ip << ":" << b.port << "):"
                  << " forwarded=" << b.forwarded.load()
                  << " connectAttempts=" << b.connectAttempts.load()
                  << " failedExchanges=" << b.failedExchanges.load()
                  << std::endl;
    }
    std::cout << "Total forwarded: " << lb::totalForwarded.load() << std::endl;
    std::cout << "Total failed:    " << lb::totalFailed.load() << std::endl;

    return 0;
}
