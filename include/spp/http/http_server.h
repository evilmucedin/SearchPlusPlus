#pragma once

#include "spp/base/status.h"
#include "spp/http/http_parser.h"
#include "spp/http/platform.h"
#include "spp/http/router.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spp::http {

struct HttpServerOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9200;
    std::size_t worker_threads = 8;
    std::size_t accept_backlog = 64;
    ParseLimits parse_limits = {};
    // Per-connection read budget guard against slow-loris-ish clients.
    std::size_t max_total_request_bytes = 64 * 1024 * 1024;
};

// Single-process HTTP/1.1 server. One acceptor thread feeds a fixed-size worker pool
// via a bounded queue of accepted client sockets. Each worker reads one request,
// dispatches through the Router, writes the response, and closes the connection
// (no keep-alive in v0.1).
class HttpServer {
 public:
    HttpServer(HttpServerOptions opts, std::shared_ptr<const Router> router);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    Status Start();
    void Stop();

    bool IsRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    std::uint16_t Port() const noexcept {
        return opts_.port;
    }

 private:
    void AcceptLoop();
    void WorkerLoop();
    void HandleClient(platform::Socket client);

    HttpServerOptions opts_;
    std::shared_ptr<const Router> router_;
    platform::Socket listening_ = platform::kInvalidSocket;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};

    std::thread acceptor_;
    std::vector<std::thread> workers_;

    std::mutex queue_mu_;
    std::condition_variable queue_cv_;
    std::deque<platform::Socket> queue_;
    static constexpr std::size_t kMaxQueueDepth = 1024;
};

}  // namespace spp::http
