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
    // 0 means "auto" — resolved at Start() to max(2 * hardware_concurrency(), 4).
    // HTTP workers accept and parse requests concurrently; search route handlers
    // gate further on `max_concurrent_searches` (below) so heavy queries don't
    // starve the box even if many parsers happen to be busy at once.
    std::size_t worker_threads = 0;
    // Cap on simultaneously-executing search route handlers. Excess searches
    // block until a slot frees up — they aren't dropped. 0 = auto, same formula
    // as worker_threads. Non-search routes (PUT /:index, POST /:index/_doc,
    // _refresh, _health, _ltr) do not consume search slots.
    std::size_t max_concurrent_searches = 0;
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

    // Highest concurrent in-flight search count observed so far. Useful for
    // verifying the concurrency cap is taking effect under load — exposed for
    // tests and operational debugging, not part of the wire protocol.
    std::size_t PeakInFlightSearches() const noexcept;
    // Resolved search-concurrency cap (0-input auto-expanded at Start()).
    std::size_t MaxConcurrentSearches() const noexcept;

 private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spp::server
