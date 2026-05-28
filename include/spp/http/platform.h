#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"

#include <cstdint>
#include <string>

namespace spp::http::platform {

// Initialize sockets (Winsock startup on Windows; no-op on POSIX).
Status InitSockets();

// Opaque socket handle. -1 / INVALID_SOCKET on failure (use IsValid()).
using Socket = std::intptr_t;
inline constexpr Socket kInvalidSocket = -1;

bool IsValid(Socket s) noexcept;
void Close(Socket s) noexcept;

// Create a listening socket bound to (host, port). Default host = "127.0.0.1".
Expected<Socket> Listen(const std::string& host, std::uint16_t port, int backlog = 64);

// Accept; blocks. Returns kInvalidSocket on shutdown via CloseListening.
Socket Accept(Socket listening);

// Read up to `max_bytes` into `out` from `client`. Returns bytes read; 0 = peer closed,
// <0 = error.
std::int64_t Read(Socket client, char* out, std::size_t max_bytes);

// Write `bytes` (loop until fully sent or error). Returns true iff all bytes were sent.
bool WriteAll(Socket client, const char* bytes, std::size_t count);

// Stop the accept loop and unblock a thread sitting in Accept().
void Shutdown(Socket listening) noexcept;

// Synchronously connect to (host, port). Caller closes the returned socket.
Expected<Socket> Connect(const std::string& host, std::uint16_t port);

}  // namespace spp::http::platform
