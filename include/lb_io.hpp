/**
 * Part B: buffered socket + connection helper shared by the LB proxy and the
 * health probers.
 *
 * A connection carries exactly one request (framing rule). A single recv()
 * can return the header line AND the start of the body, so the reader must
 * keep those extra bytes for the body reader. BufferedSocket does exactly
 * that: it holds an in-memory buffer of already-received-but-not-yet-consumed
 * bytes, and every read consumes from the buffer before touching the socket.
 */
#pragma once

#include <cstddef>
#include <string>

// lbConnectToAddress is implemented in lb_io.cpp alongside the class below.
int lbConnectToAddress(const std::string &ip, int port, int timeoutMs);

/**
 * A socket plus a small receive buffer that preserves bytes received in
 * advance of the current read (e.g. body bytes that arrived with the header).
 */
struct BufferedSocket {
    int sock;
    std::string buffer;  // received but not yet consumed

    explicit BufferedSocket(int s) : sock(s) {}

    /**
     * Read one line (up to and including the terminating '\n').
     *
     * Bytes after the newline stay in `buffer` for the next read. Returns
     * false on EOF/error before a complete line is available.
     */
    bool readLine(std::string &line);

    /**
     * Read exactly `count` bytes into out (consuming `buffer` first).
     *
     * Returns true only if all `count` bytes arrived; false on EOF/error
     * before the full count (the caller treats this as a backend failure).
     */
    bool readExact(char *out, size_t count);

    /**
     * Read up to `max` bytes into out (consuming `buffer` first) and return
     * how many were read. Returns 0 on EOF. Used to stream bodies in chunks
     * without waiting for the whole body in one recv().
     */
    size_t readUpTo(char *out, size_t max);

    /**
     * Send all of `data` (loops over short sends). Returns false on error.
     */
    bool writeAll(const char *data, size_t count);
};
