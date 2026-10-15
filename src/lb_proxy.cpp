/**
 * Part B: the per-connection proxy handler (B1/B2/B6).
 *
 * For every client connection the LB runs a single exchange:
 *
 *   1. Read + parse the client's request header (GET/PUT).
 *   2. Select a healthy backend (lbSelectBackend, B11/B12).
 *   3. Connect to that backend and forward the request line.
 *   4. Stream the backend's response back to the client, relaying the body in
 *      both directions where needed (PUT) or one direction (GET).
 *
 * Failure semantics (B6):
 *   - If the backend fails BEFORE the LB has forwarded any response byte to the
 *     client, the LB replies "ERR <reason>" and closes the connection.
 *   - If it fails AFTER response bytes have started, the LB simply closes the
 *     client connection — the client detects the failure as a short read, since
 *     the body is a raw byte stream with no room for an error message.
 *
 * A connection carries exactly one request (framing rule); the client opens a
 * new connection for the next transfer.
 */
#include "load_balancer.hpp"

#include "lb_io.hpp"
#include "framing.hpp"
#include "response.hpp"
#include "util.hpp"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <unistd.h>

// How long we wait on the client to send its header before giving up. Matches
// the Part A server (A5: parsing must not block admission / pin a thread).
constexpr int CLIENT_HEADER_TIMEOUT_SECONDS = 5;
// Connect timeout when reaching a backend (B14 relies on fast refusal).
constexpr int BACKEND_CONNECT_TIMEOUT_MS = 2000;
// Chunk size used when streaming bodies between the client and the backend.
constexpr size_t BODY_CHUNK = 65536;

namespace {

enum class Outcome {
    Success,             // the full response was relayed to the client
    FailedBeforeResponse, // backend failed before any response byte was relayed
    FailedAfterResponse   // backend failed after response bytes were relayed
};

/**
 * Parse an "OK <n>\n" line into its numeric value.
 * (The Part A client's parser is file-local, so the LB keeps its own copy.)
 */
bool parseOkValue(const std::string &line, size_t &value) {
    if (line.rfind("OK ", 0) != 0) {
        return false;
    }
    std::string number = line.substr(3);
    if (!number.empty() && number.back() == '\n') {
        number.pop_back();
    }
    try {
        size_t consumed = 0;
        unsigned long long parsed = std::stoull(number, &consumed);
        if (consumed != number.size()) {
            return false;
        }
        value = static_cast<size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * Stream exactly `total` bytes from `src` to `dst` in chunks.
 *
 * Returns false if the source ends early (backend failure after response,
 * B6) or if the destination write fails (client went away).
 */
bool streamBytes(BufferedSocket &src, BufferedSocket &dst, size_t total) {
    if (total == 0) {
        return true;
    }
    char chunk[BODY_CHUNK];
    size_t moved = 0;
    while (moved < total) {
        size_t want = std::min(sizeof(chunk), total - moved);
        size_t got = src.readUpTo(chunk, want);
        if (got == 0) {
            return false;  // backend closed early: partial body
        }
        if (!dst.writeAll(chunk, got)) {
            return false;  // client disconnected mid-transfer
        }
        moved += got;
    }
    return true;
}

/**
 * Relay a single request/response exchange between the client and one backend.
 *
 * requestLine  : the exact request line as received from the client (incl. '\n').
 * bodyPrefix   : any body bytes that were already buffered with the header read
 *                (only meaningful for PUT; usually empty).
 *
 * Neither socket is closed here — the caller is responsible for closing both.
 */
Outcome relayExchange(BufferedSocket &client, BufferedSocket &backend,
                      const Request &req,
                      const std::string &requestLine,
                      const std::string &bodyPrefix) {
    // 1) Forward the request line to the backend.
    if (!backend.writeAll(requestLine.data(), requestLine.size())) {
        return Outcome::FailedBeforeResponse;
    }

    // 2) Read the backend's response header line.
    std::string respLine;
    if (!backend.readLine(respLine)) {
        return Outcome::FailedBeforeResponse;
    }

    // 3) Forward the response header to the client. Past this point the client
    //    has started receiving a response, so later failures are "after response".
    if (!client.writeAll(respLine.data(), respLine.size())) {
        return Outcome::FailedAfterResponse;
    }

    // 4) An ERR line means the request failed but the exchange completed cleanly.
    if (respLine.rfind("ERR ", 0) == 0) {
        return Outcome::Success;
    }

    size_t n = 0;
    if (!parseOkValue(respLine, n)) {
        return Outcome::FailedAfterResponse;  // malformed backend response
    }

    if (req.type == REQUEST_GET) {
        // GET: <n> is the file size; stream exactly that many bytes to the client.
        if (!streamBytes(backend, client, n)) {
            return Outcome::FailedAfterResponse;
        }
        return Outcome::Success;
    }

    // PUT: the first OK carries 0 (ack). The client then sends the body.
    if (n != 0) {
        return Outcome::FailedAfterResponse;  // protocol mismatch
    }

    size_t bodyTotal = req.bytes;
    size_t bodyRelayed = 0;

    // 4a) Forward any body bytes already buffered from the header read.
    size_t prefix = std::min(bodyPrefix.size(), bodyTotal);
    if (prefix > 0) {
        if (!backend.writeAll(bodyPrefix.data(), prefix)) {
            return Outcome::FailedAfterResponse;
        }
        bodyRelayed += prefix;
    }

    // 4b) Relay the rest of the body from the client to the backend.
    while (bodyRelayed < bodyTotal) {
        char chunk[BODY_CHUNK];
        size_t want = std::min(sizeof(chunk), bodyTotal - bodyRelayed);
        size_t got = client.readUpTo(chunk, want);
        if (got == 0) {
            return Outcome::FailedAfterResponse;  // client stopped sending
        }
        if (!backend.writeAll(chunk, got)) {
            return Outcome::FailedAfterResponse;  // backend gone
        }
        bodyRelayed += got;
    }

    // 4c) Read the backend's final response line and forward it to the client.
    std::string finalLine;
    if (!backend.readLine(finalLine)) {
        return Outcome::FailedAfterResponse;
    }
    if (!client.writeAll(finalLine.data(), finalLine.size())) {
        return Outcome::FailedAfterResponse;
    }
    return Outcome::Success;
}

}  // namespace

void lbHandleClientConnection(int clientSocket) {
    // Register as active so a graceful shutdown can wait for us to finish (B5).
    lb::activeHandlers.fetch_add(1);

    int backendSocket = -1;

    auto finish = [&]() {
        if (backendSocket >= 0) {
            close(backendSocket);
        }
        close(clientSocket);
        lb::activeHandlers.fetch_sub(1);
        // Always notify (cheap); the shutdown wait re-checks its predicate,
        // so a spurious wakeup is harmless and a missed one is avoided.
        std::lock_guard<std::mutex> g(lb::activeMutex);
        lb::activeCv.notify_all();
    };

    // Prevent a silent client from pinning this thread (A5/B4 robustness).
    setReceiveTimeout(clientSocket, CLIENT_HEADER_TIMEOUT_SECONDS);

    BufferedSocket client(clientSocket);

    // 1) Read + parse the client's request header.
    std::string headerLine;
    if (!client.readLine(headerLine)) {
        finish();
        return;
    }

    ParseResult pr = parseRequest(headerLine);
    if (!pr.success) {
        sendErrorResponse(clientSocket, pr.errorMessage);
        finish();
        return;
    }

    // HEALTH is an LB-to-backend probe operation, not a client operation.
    // Answer with ERR (protocol-safe) rather than forwarding (B6/A24).
    if (pr.request.type == REQUEST_HEALTH) {
        sendErrorResponse(clientSocket, "HEALTH is not forwarded by the load balancer");
        finish();
        return;
    }

    // Body bytes that arrived together with the header (usually empty).
    std::string bodyPrefix = client.buffer;

    // 2) Pick a backend.
    int bIdx = lbSelectBackend();
    if (bIdx < 0) {
        lb::totalFailed.fetch_add(1);
        sendErrorResponse(clientSocket, "no available backends");
        finish();
        return;
    }

    Backend &b = lb::backends[bIdx];
    b.connectAttempts.fetch_add(1);

    // 3) Connect to the chosen backend.
    backendSocket = lbConnectToAddress(b.ip, b.port, BACKEND_CONNECT_TIMEOUT_MS);
    if (backendSocket < 0) {
        // Unreachable before any response byte was forwarded (B6). Mark it
        // unhealthy so selection excludes it until a probe restores it (B10).
        b.healthy.store(false);
        lb::totalFailed.fetch_add(1);
        sendErrorResponse(clientSocket, "backend connection failed: " + b.name);
        finish();
        return;
    }

    // 4) Commit the request to this backend (B5 forwarded count, B12 in-flight).
    b.forwarded.fetch_add(1);
    b.inFlight.fetch_add(1);
    lb::totalForwarded.fetch_add(1);

    BufferedSocket backend(backendSocket);
    Outcome outcome = relayExchange(client, backend, pr.request, headerLine, bodyPrefix);
    b.inFlight.fetch_sub(1);

    if (outcome != Outcome::Success) {
        b.failedExchanges.fetch_add(1);
        lb::totalFailed.fetch_add(1);
        if (outcome == Outcome::FailedBeforeResponse) {
            sendErrorResponse(clientSocket, "backend failure before response");
        }
        // FailedAfterResponse: the client will observe a short read (B6).
    }

    finish();
}
