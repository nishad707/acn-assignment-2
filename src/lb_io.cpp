/**
 * Part B: implementation of the buffered socket and the connect helper.
 */
#include "lb_io.hpp"

#include "response.hpp"  // reuse sendAllBytes for writeAll

#include <arpa/inet.h>
#include <algorithm>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/**
 * Open a TCP connection to ip:port with a send/recv timeout of timeoutMs.
 *
 * Returns the connected socket fd on success, or -1 on failure (refused,
 * timeout, ...). On loopback the connect either succeeds immediately or is
 * refused immediately once the backend has been killed, so this returns
 * quickly either way — which is what the B14 fault-injection test relies on.
 */
int lbConnectToAddress(const std::string &ip, int port, int timeoutMs) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    // A timeout on both send and recv ensures a wedged backend can never pin
    // a handler or prober thread indefinitely (robustness of A5/B7).
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

bool BufferedSocket::readLine(std::string &line) {
    line.clear();
    while (true) {
        // If the buffer already holds a full line, take it and keep the rest.
        size_t pos = buffer.find('\n');
        if (pos != std::string::npos) {
            line = buffer.substr(0, pos + 1);  // include the '\n'
            buffer.erase(0, pos + 1);
            return true;
        }
        // Otherwise pull more data from the socket into the buffer.
        char tmp[4096];
        ssize_t n = recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            return false;  // EOF or error: no complete line available
        }
        buffer.append(tmp, static_cast<size_t>(n));
    }
}

bool BufferedSocket::readExact(char *out, size_t count) {
    size_t got = 0;
    while (got < count) {
        // Consume buffered bytes first (they may have arrived with the header).
        if (!buffer.empty()) {
            size_t n = std::min(buffer.size(), count - got);
            std::memcpy(out + got, buffer.data(), n);
            buffer.erase(0, n);
            got += n;
            continue;
        }
        ssize_t r = recv(sock, out + got, count - got, 0);
        if (r <= 0) {
            return false;  // backend closed early: partial body
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

size_t BufferedSocket::readUpTo(char *out, size_t max) {
    if (max == 0) {
        return 0;
    }
    // Use whatever is already buffered, up to `max`.
    if (!buffer.empty()) {
        size_t n = std::min(buffer.size(), max);
        std::memcpy(out, buffer.data(), n);
        buffer.erase(0, n);
        return n;
    }
    ssize_t r = recv(sock, out, max, 0);
    return r > 0 ? static_cast<size_t>(r) : 0;
}

bool BufferedSocket::writeAll(const char *data, size_t count) {
    return sendAllBytes(sock, data, count);
}
