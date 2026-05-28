#include "spp/http/platform.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
using socklen_t_alias = int;
#define SPP_INVALID_SOCKET (INVALID_SOCKET)
#define SPP_CLOSESOCKET(s) closesocket(static_cast<SOCKET>(s))
#define SPP_SHUTDOWN(s, how) shutdown(static_cast<SOCKET>(s), (how))
#define SPP_SHUT_RDWR SD_BOTH
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
using socklen_t_alias = socklen_t;
#define SPP_INVALID_SOCKET (-1)
#define SPP_CLOSESOCKET(s) ::close(static_cast<int>(s))
#define SPP_SHUTDOWN(s, how) ::shutdown(static_cast<int>(s), (how))
#define SPP_SHUT_RDWR SHUT_RDWR
#endif

namespace spp::http::platform {

namespace {

Status SocketError(const char* what) {
#if defined(_WIN32)
    char buf[256];
    DWORD wsa = WSAGetLastError();
    std::snprintf(buf, sizeof(buf), "%s: WSA error %lu", what, static_cast<unsigned long>(wsa));
    return Status::IoError(buf);
#else
    std::string msg = what;
    msg += ": ";
    msg += std::strerror(errno);
    return Status::IoError(std::move(msg));
#endif
}

}  // namespace

Status InitSockets() {
#if defined(_WIN32)
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0)
        return Status::IoError("WSAStartup failed");
#else
    // Don't let SIGPIPE kill the server when a client disconnects mid-write.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    return Status::Ok();
}

bool IsValid(Socket s) noexcept {
    return s != SPP_INVALID_SOCKET;
}

void Close(Socket s) noexcept {
    if (!IsValid(s))
        return;
    SPP_CLOSESOCKET(s);
}

Expected<Socket> Listen(const std::string& host, std::uint16_t port, int backlog) {
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == SPP_INVALID_SOCKET)
        return SocketError("socket()");

    int yes = 1;
#if defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        SPP_CLOSESOCKET(s);
        return Status::InvalidArgument("invalid bind host: " + host);
    }

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        Status st = SocketError("bind()");
        SPP_CLOSESOCKET(s);
        return st;
    }
    if (::listen(s, backlog) != 0) {
        Status st = SocketError("listen()");
        SPP_CLOSESOCKET(s);
        return st;
    }
    return static_cast<Socket>(s);
}

Socket Accept(Socket listening) {
    socket_t l = static_cast<socket_t>(listening);
    sockaddr_storage peer{};
    socklen_t_alias len = sizeof(peer);
    socket_t c = ::accept(l, reinterpret_cast<sockaddr*>(&peer), &len);
    if (c == SPP_INVALID_SOCKET)
        return kInvalidSocket;
    return static_cast<Socket>(c);
}

std::int64_t Read(Socket client, char* out, std::size_t max_bytes) {
#if defined(_WIN32)
    int n = ::recv(static_cast<SOCKET>(client), out, static_cast<int>(max_bytes), 0);
    return static_cast<std::int64_t>(n);
#else
    ssize_t n = ::recv(static_cast<int>(client), out, max_bytes, 0);
    return static_cast<std::int64_t>(n);
#endif
}

bool WriteAll(Socket client, const char* bytes, std::size_t count) {
    std::size_t sent = 0;
    while (sent < count) {
#if defined(_WIN32)
        int n =
            ::send(static_cast<SOCKET>(client), bytes + sent, static_cast<int>(count - sent), 0);
#else
        ssize_t n = ::send(static_cast<int>(client), bytes + sent, count - sent, 0);
#endif
        if (n <= 0)
            return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void Shutdown(Socket listening) noexcept {
    if (!IsValid(listening))
        return;
    SPP_SHUTDOWN(listening, SPP_SHUT_RDWR);
    SPP_CLOSESOCKET(listening);
}

Expected<Socket> Connect(const std::string& host, std::uint16_t port) {
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == SPP_INVALID_SOCKET)
        return SocketError("socket()");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    const std::string h = host.empty() ? std::string("127.0.0.1") : host;
    if (inet_pton(AF_INET, h.c_str(), &addr.sin_addr) != 1) {
        SPP_CLOSESOCKET(s);
        return Status::InvalidArgument("invalid host: " + host);
    }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        Status st = SocketError("connect()");
        SPP_CLOSESOCKET(s);
        return st;
    }
    return static_cast<Socket>(s);
}

}  // namespace spp::http::platform
