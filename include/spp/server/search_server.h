#pragma once

#include "spp/base/status.h"
#include "spp/http/http_message.h"
#include "spp/http/http_server.h"
#include "spp/http/router.h"
#include "spp/index/index_writer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace spp::server {

struct SearchServerOptions {
    std::filesystem::path index_dir;  // Top-level dir holding per-index subdirectories.
    std::string host = "127.0.0.1";
    std::uint16_t port = 9200;
    std::size_t worker_threads = 8;
};

// A single-process, multi-index search server.
// Each subdirectory of `index_dir` is one index (created via PUT /:index).
class SearchServer {
 public:
    explicit SearchServer(SearchServerOptions opts);
    ~SearchServer();
    SearchServer(const SearchServer&) = delete;
    SearchServer& operator=(const SearchServer&) = delete;

    Status Start();
    void Stop();

    std::uint16_t Port() const noexcept;

 private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spp::server
