#include "spp/http/http_server.h"

#include "spp/base/logging.h"
#include "spp/http/http_message.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace spp::http {

HttpServer::HttpServer(HttpServerOptions opts, std::shared_ptr<const Router> router)
    : opts_(std::move(opts)), router_(std::move(router)) {}

HttpServer::~HttpServer() {
    Stop();
}

Status HttpServer::Start() {
    if (running_.load())
        return Status::FailedPrecondition("server already running");
    if (auto st = platform::InitSockets(); !st.ok())
        return st;

    auto sock = platform::Listen(opts_.host, opts_.port, static_cast<int>(opts_.accept_backlog));
    if (!sock.ok())
        return sock.status();
    listening_ = sock.value();

    stopping_.store(false);
    running_.store(true);
    workers_.reserve(opts_.worker_threads);
    for (std::size_t i = 0; i < opts_.worker_threads; ++i) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
    acceptor_ = std::thread([this] { AcceptLoop(); });
    SPP_LOG_INFO("HttpServer started on %s:%u (workers=%zu)",
                 opts_.host.c_str(),
                 static_cast<unsigned>(opts_.port),
                 opts_.worker_threads);
    return Status::Ok();
}

void HttpServer::Stop() {
    if (!running_.exchange(false))
        return;
    stopping_.store(true);

    if (platform::IsValid(listening_)) {
        platform::Shutdown(listening_);
        listening_ = platform::kInvalidSocket;
    }
    if (acceptor_.joinable())
        acceptor_.join();

    {
        std::lock_guard<std::mutex> lk(queue_mu_);
        for (auto s : queue_)
            platform::Close(s);
        queue_.clear();
    }
    queue_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable())
            t.join();
    }
    workers_.clear();
    SPP_LOG_INFO("HttpServer stopped");
}

void HttpServer::AcceptLoop() {
    while (!stopping_.load()) {
        platform::Socket c = platform::Accept(listening_);
        if (!platform::IsValid(c)) {
            if (stopping_.load())
                break;
            // Transient accept failure (EINTR etc.) — keep going.
            continue;
        }
        std::unique_lock<std::mutex> lk(queue_mu_);
        if (queue_.size() >= kMaxQueueDepth) {
            lk.unlock();
            // Drop the connection rather than hold up the acceptor.
            HttpResponse resp;
            resp.status = 503;
            resp.body = R"({"error":{"type":"overloaded","reason":"queue full"}})";
            const auto wire = SerializeResponse(resp);
            platform::WriteAll(c, wire.data(), wire.size());
            platform::Close(c);
            continue;
        }
        queue_.push_back(c);
        lk.unlock();
        queue_cv_.notify_one();
    }
}

void HttpServer::WorkerLoop() {
    while (true) {
        platform::Socket client;
        {
            std::unique_lock<std::mutex> lk(queue_mu_);
            queue_cv_.wait(lk, [&] { return !queue_.empty() || stopping_.load(); });
            if (queue_.empty())
                return;  // Stopping.
            client = queue_.front();
            queue_.pop_front();
        }
        HandleClient(client);
    }
}

namespace {

HttpResponse MakeError(int status, std::string_view reason) {
    HttpResponse resp;
    resp.status = status;
    resp.body = std::string(R"({"error":{"type":"bad_request","reason":")") + std::string(reason) +
                R"("}})";
    return resp;
}

}  // namespace

void HttpServer::HandleClient(platform::Socket client) {
    std::string buf;
    buf.reserve(8 * 1024);
    char scratch[8192];

    HttpRequest req;
    while (true) {
        ParseResult pr = TryParseRequest(buf, opts_.parse_limits, req);
        if (pr.status == ParseStatus::kComplete) {
            break;
        }
        if (pr.status == ParseStatus::kError) {
            auto resp = MakeError(pr.http_status_on_error ? pr.http_status_on_error : 400,
                                  pr.error_message);
            const auto wire = SerializeResponse(resp);
            platform::WriteAll(client, wire.data(), wire.size());
            platform::Close(client);
            return;
        }
        if (buf.size() > opts_.max_total_request_bytes) {
            auto resp = MakeError(413, "request too large");
            const auto wire = SerializeResponse(resp);
            platform::WriteAll(client, wire.data(), wire.size());
            platform::Close(client);
            return;
        }
        std::int64_t n = platform::Read(client, scratch, sizeof(scratch));
        if (n <= 0) {
            platform::Close(client);
            return;
        }
        buf.append(scratch, static_cast<std::size_t>(n));
    }

    HttpResponse resp;
    try {
        resp = router_->Dispatch(req);
    } catch (const std::exception& e) {
        resp = MakeError(500, e.what());
    } catch (...) {
        resp = MakeError(500, "unknown handler error");
    }

    const auto wire = SerializeResponse(resp);
    platform::WriteAll(client, wire.data(), wire.size());
    platform::Close(client);
}

}  // namespace spp::http
