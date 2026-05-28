// spp_serve — HTTP search server.
//
// Usage:
//   spp_serve --index <root_dir> [--host 127.0.0.1] [--port 9200]
//             [--workers N] [--max-searches N]
//
// --workers and --max-searches default to max(2 * CPU, 4). Search routes
// additionally gate on --max-searches so heavy queries can't starve the box
// regardless of how many HTTP workers happen to be idle.

#include "spp/server/search_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int /*sig*/) {
    g_stop.store(true);
}

void Usage() {
    std::fputs(
        "Usage: spp_serve --index <root_dir> [--host 127.0.0.1] [--port 9200]\n"
        "                 [--workers N] [--max-searches N]\n"
        "  --workers, --max-searches default to max(2 * CPU, 4).\n",
        stderr);
}

}  // namespace

int main(int argc, char** argv) {
    spp::server::SearchServerOptions opts;
    opts.host = "127.0.0.1";
    opts.port = 9200;
    // worker_threads / max_concurrent_searches stay 0 so SearchServer's auto
    // path (2 * hardware_concurrency, floor 4) kicks in.

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--index" && i + 1 < argc)
            opts.index_dir = argv[++i];
        else if (a == "--host" && i + 1 < argc)
            opts.host = argv[++i];
        else if (a == "--port" && i + 1 < argc)
            opts.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (a == "--workers" && i + 1 < argc)
            opts.worker_threads = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (a == "--max-searches" && i + 1 < argc)
            opts.max_concurrent_searches = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (a == "-h" || a == "--help") {
            Usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            Usage();
            return 2;
        }
    }
    if (opts.index_dir.empty()) {
        Usage();
        return 2;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    spp::server::SearchServer server(std::move(opts));
    if (auto st = server.Start(); !st.ok()) {
        std::fprintf(stderr, "start failed: %s\n", st.message().c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "spp_serve running (max_concurrent_searches=%zu). Press Ctrl-C to stop.\n",
                 server.MaxConcurrentSearches());

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    server.Stop();
    return 0;
}
