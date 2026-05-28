#include "spp/server/search_server.h"

#include "spp/base/expected.h"
#include "spp/base/logging.h"
#include "spp/base/status.h"
#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/json/json_parser.h"
#include "spp/json/json_serializer.h"
#include "spp/json/json_value.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spp::server {

namespace {

http::HttpResponse JsonResponse(int status, spp::json::JsonValue v) {
    http::HttpResponse resp;
    resp.status = status;
    resp.body = spp::json::Serialize(v);
    resp.SetHeader("Content-Type", "application/json");
    return resp;
}

http::HttpResponse ErrorJson(int status, std::string_view type, std::string_view reason) {
    spp::json::JsonObject err;
    err["type"] = std::string(type);
    err["reason"] = std::string(reason);
    spp::json::JsonObject root;
    root["error"] = spp::json::JsonValue(std::move(err));
    return JsonResponse(status, spp::json::JsonValue(std::move(root)));
}

std::string Sanitize(std::string_view raw_name) {
    std::string out;
    out.reserve(raw_name.size());
    for (char c : raw_name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.') {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

class SearchServer::Impl {
 public:
    explicit Impl(SearchServerOptions opts) : opts_(std::move(opts)) {}

    Status Start() {
        std::error_code ec;
        std::filesystem::create_directories(opts_.index_dir, ec);
        if (ec) {
            return Status::IoError("create index root: " + ec.message());
        }
        for (const auto& entry : std::filesystem::directory_iterator(opts_.index_dir, ec)) {
            if (!entry.is_directory())
                continue;
            const std::string name = entry.path().filename().string();
            auto w = index::IndexWriter::Open(entry.path());
            if (!w.ok()) {
                SPP_LOG_INFO(
                    "skipping non-index dir %s: %s", name.c_str(), w.status().message().c_str());
                continue;
            }
            writers_.emplace(name, std::move(*w));
        }

        router_ = std::make_shared<http::Router>();
        BuildRoutes();

        http::HttpServerOptions http_opts;
        http_opts.host = opts_.host;
        http_opts.port = opts_.port;
        http_opts.worker_threads = opts_.worker_threads;
        http_server_ = std::make_unique<http::HttpServer>(std::move(http_opts), router_);
        return http_server_->Start();
    }

    void Stop() {
        if (http_server_) {
            http_server_->Stop();
            http_server_.reset();
        }
        std::lock_guard<std::mutex> lk(writers_mu_);
        for (auto& [name, w] : writers_) {
            // Clean shutdown: seal any pending docs, then close.
            (void)w->Refresh();
            (void)w->Close();
        }
        writers_.clear();
    }

    std::uint16_t Port() const noexcept {
        return http_server_ ? http_server_->Port() : opts_.port;
    }

 private:
    void BuildRoutes() {
        router_->Add("GET", "/_health", [this](const http::HttpRequest& r) { return Health(r); });
        router_->Add(
            "PUT", "/:index", [this](const http::HttpRequest& r) { return CreateIndex(r); });
        router_->Add(
            "POST", "/:index/_doc", [this](const http::HttpRequest& r) { return PostDoc(r); });
        router_->Add(
            "POST", "/:index/_refresh", [this](const http::HttpRequest& r) { return Refresh(r); });
        router_->Add(
            "GET", "/:index/_search", [this](const http::HttpRequest& r) { return Search(r); });
    }

    http::HttpResponse Health(const http::HttpRequest&) {
        spp::json::JsonObject root;
        root["status"] = std::string("ok");
        return JsonResponse(200, spp::json::JsonValue(std::move(root)));
    }

    index::IndexWriter* GetWriter(const std::string& name) {
        std::lock_guard<std::mutex> lk(writers_mu_);
        auto it = writers_.find(name);
        return it == writers_.end() ? nullptr : it->second.get();
    }

    http::HttpResponse CreateIndex(const http::HttpRequest& req) {
        auto it = req.path_params.find("index");
        if (it == req.path_params.end())
            return ErrorJson(400, "bad_request", "missing index name");
        const std::string raw = it->second;
        const std::string name = Sanitize(raw);
        if (name.empty() || name != raw) {
            return ErrorJson(400, "invalid_index_name", "index name has forbidden characters");
        }

        auto parsed = spp::json::Parse(req.body);
        if (!parsed.ok()) {
            return ErrorJson(400, "bad_request", parsed.status().message());
        }
        const spp::json::JsonValue* mappings_v = parsed->find("mappings");
        if (mappings_v == nullptr || !mappings_v->is_object()) {
            return ErrorJson(400, "bad_request", R"(missing "mappings" object)");
        }
        auto schema_e = index::Schema::FromMappingsJson(*mappings_v);
        if (!schema_e.ok()) {
            return ErrorJson(400, "bad_request", schema_e.status().message());
        }

        const auto dir = opts_.index_dir / name;
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            std::lock_guard<std::mutex> lk(writers_mu_);
            if (writers_.count(name)) {
                return ErrorJson(409, "already_exists", "index already exists");
            }
        }
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return ErrorJson(500, "io_error", ec.message());

        index::IndexOpenOptions opts;
        opts.initial_schema = &*schema_e;
        auto writer_e = index::IndexWriter::Open(dir, opts);
        if (!writer_e.ok()) {
            return ErrorJson(500, "internal", writer_e.status().message());
        }

        {
            std::lock_guard<std::mutex> lk(writers_mu_);
            writers_[name] = std::move(*writer_e);
        }
        spp::json::JsonObject ok;
        ok["acknowledged"] = true;
        ok["index"] = name;
        return JsonResponse(200, spp::json::JsonValue(std::move(ok)));
    }

    http::HttpResponse PostDoc(const http::HttpRequest& req) {
        auto it = req.path_params.find("index");
        if (it == req.path_params.end())
            return ErrorJson(400, "bad_request", "missing index name");
        const std::string name = it->second;
        index::IndexWriter* w = GetWriter(name);
        if (w == nullptr)
            return ErrorJson(404, "not_found", "no such index");

        auto parsed_e = spp::json::Parse(req.body);
        if (!parsed_e.ok()) {
            return ErrorJson(400, "bad_request", parsed_e.status().message());
        }
        const auto& parsed = *parsed_e;
        if (!parsed.is_object()) {
            return ErrorJson(400, "bad_request", "doc must be a JSON object");
        }

        index::Document doc;
        const auto& obj = parsed.as_object();
        if (auto idv = parsed.find("_id"); idv != nullptr && idv->is_string()) {
            doc.id = idv->as_string();
        }
        for (const auto& [k, v] : obj) {
            if (k == "_id")
                continue;
            if (v.is_string()) {
                doc.fields[k] = v.as_string();
            } else if (v.is_number()) {
                doc.fields[k] = std::to_string(v.as_double());
            } else if (v.is_bool()) {
                doc.fields[k] = v.as_bool() ? "true" : "false";
            }
            // Arrays/objects in v0.1 are not indexed (silently skipped).
        }

        if (auto st = w->AddDocument(doc, req.body); !st.ok()) {
            return ErrorJson(400, "bad_request", st.message());
        }
        spp::json::JsonObject ok;
        ok["result"] = std::string("created");
        ok["_id"] = doc.id;
        ok["_index"] = name;
        return JsonResponse(201, spp::json::JsonValue(std::move(ok)));
    }

    http::HttpResponse Refresh(const http::HttpRequest& req) {
        auto it = req.path_params.find("index");
        if (it == req.path_params.end())
            return ErrorJson(400, "bad_request", "missing index name");
        const std::string name = it->second;
        index::IndexWriter* w = GetWriter(name);
        if (w == nullptr)
            return ErrorJson(404, "not_found", "no such index");
        auto gen_e = w->Refresh();
        if (!gen_e.ok())
            return ErrorJson(500, "internal", gen_e.status().message());
        spp::json::JsonObject ok;
        ok["refreshed"] = true;
        ok["generation"] = static_cast<std::int64_t>(*gen_e);
        return JsonResponse(200, spp::json::JsonValue(std::move(ok)));
    }

    http::HttpResponse Search(const http::HttpRequest& req) {
        auto it = req.path_params.find("index");
        if (it == req.path_params.end())
            return ErrorJson(400, "bad_request", "missing index name");
        const std::string name = it->second;
        index::IndexWriter* w = GetWriter(name);
        if (w == nullptr)
            return ErrorJson(404, "not_found", "no such index");

        auto reader = w->CurrentReader();
        if (!reader)
            return ErrorJson(503, "unavailable", "no reader yet");

        const auto params = req.QueryParams();
        std::string q;
        if (auto pi = params.find("q"); pi != params.end())
            q = pi->second;
        std::size_t size = 10;
        if (auto pi = params.find("size"); pi != params.end()) {
            try {
                size = static_cast<std::size_t>(std::stoul(pi->second));
            } catch (...) {
                return ErrorJson(400, "bad_request", "invalid 'size' parameter");
            }
            if (size > 1000)
                size = 1000;
        }
        std::string default_field;
        if (auto pi = params.find("default_field"); pi != params.end()) {
            default_field = pi->second;
        } else {
            // Pick the first non-_id text field as the default.
            for (const auto& f : reader->schema().fields()) {
                if (f.name != "_id" && f.type == index::FieldType::kText) {
                    default_field = f.name;
                    break;
                }
            }
            if (default_field.empty())
                default_field = "_id";
        }

        auto ast_e = spp::query::Parse(q, default_field);
        if (!ast_e.ok()) {
            return ErrorJson(400, "bad_request", ast_e.status().message());
        }
        spp::query::Searcher searcher(reader);
        spp::query::SearchOptions sopts;
        sopts.size = size;
        sopts.default_field = default_field;
        auto res_e = searcher.Search(*ast_e, sopts);
        if (!res_e.ok()) {
            return ErrorJson(500, "internal", res_e.status().message());
        }

        spp::json::JsonObject root;
        root["total"] = static_cast<std::int64_t>(res_e->total_hits);
        spp::json::JsonArray hits;
        hits.reserve(res_e->hits.size());
        for (const auto& h : res_e->hits) {
            spp::json::JsonObject one;
            one["_id"] = h.id;
            one["_score"] = h.score;
            if (!h.stored_fields_json.empty()) {
                auto sf = spp::json::Parse(h.stored_fields_json);
                if (sf.ok())
                    one["_source"] = *sf;
            }
            hits.emplace_back(std::move(one));
        }
        root["hits"] = spp::json::JsonValue(std::move(hits));
        return JsonResponse(200, spp::json::JsonValue(std::move(root)));
    }

    SearchServerOptions opts_;
    std::shared_ptr<http::Router> router_;
    std::unique_ptr<http::HttpServer> http_server_;
    std::mutex writers_mu_;
    std::unordered_map<std::string, std::unique_ptr<index::IndexWriter>> writers_;
};

SearchServer::SearchServer(SearchServerOptions opts)
    : impl_(std::make_unique<Impl>(std::move(opts))) {}

SearchServer::~SearchServer() {
    Stop();
}

Status SearchServer::Start() {
    return impl_->Start();
}
void SearchServer::Stop() {
    if (impl_)
        impl_->Stop();
}
std::uint16_t SearchServer::Port() const noexcept {
    return impl_->Port();
}

}  // namespace spp::server
