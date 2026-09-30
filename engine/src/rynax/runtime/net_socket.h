#pragma once

// Plain sockets on Windows, macOS and Linux (not in web builds): the few differences, in one place.
// Used by multiplayer (network.cpp) and the online relay (relay.cpp).

#if !defined(__EMSCRIPTEN__)

#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define RYNAX_CLOSE closesocket
#define RYNAX_NOSIGNAL 0
#define RYNAX_POLL WSAPoll
using rynax_pollfd = WSAPOLLFD;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define RYNAX_CLOSE ::close
#if defined(MSG_NOSIGNAL)
#define RYNAX_NOSIGNAL MSG_NOSIGNAL
#else
#define RYNAX_NOSIGNAL 0
#endif
#define RYNAX_POLL ::poll
using rynax_pollfd = pollfd;
#endif

namespace rynax::net {

inline void socketsReady() {
#if defined(_WIN32)
    static bool started = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)started;
#endif
}

inline void setNonBlocking(socket_t s) {
#if defined(_WIN32)
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

inline bool wouldBlock() {
#if defined(_WIN32)
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

inline void noDelay(socket_t s) {
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
#if defined(SO_NOSIGPIPE)
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

// Starts connecting to "host:port" (a name or an address; IPv4 or IPv6) without waiting for the
// connection. INVALID_SOCKET, with `error` set, if the name doesn't resolve.
inline socket_t connectTo(const std::string& host, int port, std::string& error) {
    socketsReady();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "Couldn't find " + host + " (check the address and the internet connection).";
        return INVALID_SOCKET;
    }
    socket_t s = INVALID_SOCKET;
    for (addrinfo* a = found; a; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET)
            continue;
        setNonBlocking(s);
        noDelay(s);
        if (::connect(s, a->ai_addr, static_cast<socklen_t>(a->ai_addrlen)) == 0 || wouldBlock())
            break;
        RYNAX_CLOSE(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(found);
    if (s == INVALID_SOCKET)
        error = "Couldn't reach " + host + ".";
    return s;
}

// "relay.example.com:4243" -> host and port (the default when there's none). "[::1]:4243" too.
inline void splitAddress(const std::string& address, std::string& host, int& port, int defaultPort) {
    host = address;
    port = defaultPort;
    size_t colon = address.rfind(':');
    if (!address.empty() && address[0] == '[') {
        size_t close = address.find(']');
        host = address.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        if (close != std::string::npos && close + 1 < address.size() && address[close + 1] == ':')
            port = std::atoi(address.c_str() + close + 2);
    } else if (colon != std::string::npos && address.find(':') == colon) { // one colon: host:port
        host = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
    }
    if (port <= 0 || port > 65535)
        port = defaultPort;
}

} // namespace rynax::net

#endif
