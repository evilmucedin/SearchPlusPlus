// hello_search — minimal end-to-end use of the SearchPlusPlus C++ library.
//
// Build (from the repo root, examples are on by default):
//   cmake --preset default
//   cmake --build --preset default --target hello_search
//   ./build/default/examples/hello_search/hello_search

#include "spp/spp.h"

#include <cstdio>
#include <filesystem>
#include <random>

namespace {

std::filesystem::path MakeTempIndexDir() {
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() / ("spp_hello_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return dir;
}

int Die(const char* what, const spp::Status& s) {
    std::fprintf(stderr, "hello_search: %s: %s\n", what, s.message().c_str());
    return 1;
}

struct Doc {
    std::string id, title, body;
};

}  // namespace

int main() {
    spp::index::Schema schema;
    if (auto st = schema.AddTextFields({"title", "body"}); !st.ok())
        return Die("AddTextFields", st);

    const auto dir = MakeTempIndexDir();
    spp::index::IndexOpenOptions open_opts;
    open_opts.initial_schema = &schema;
    auto writer = spp::index::IndexWriter::Open(dir.string(), open_opts);
    if (!writer.ok())
        return Die("Open", writer.status());

    const std::vector<Doc> corpus = {
        {"a", "Search engines", "How full-text engines tokenize, index, and rank."},
        {"b", "Inverted index", "An inverted index maps each term to the docs containing it."},
        {"c", "BM25 explained", "BM25 scores relevance via term frequency and document length."},
        {"d", "Posting lists", "Posting lists store doc ids and term frequencies."},
        {"e", "Tokenizer basics", "A tokenizer splits text into the tokens the index stores."},
    };
    for (const auto& d : corpus) {
        spp::index::Document doc;
        doc.id = d.id;
        doc.fields["title"] = d.title;
        doc.fields["body"] = d.body;
        if (auto st = (*writer)->AddDocument(doc); !st.ok())
            return Die("AddDocument", st);
    }
    if (auto g = (*writer)->Refresh(); !g.ok())
        return Die("Refresh", g.status());

    spp::query::Searcher s((*writer)->CurrentReader());
    auto run = [&](const char* q, const char* field) {
        spp::query::SearchOptions opts;
        opts.default_field = field;
        opts.size = 5;
        auto res = s.Search(q, opts);
        if (!res.ok()) {
            std::fprintf(stderr, "  search error: %s\n", res.status().message().c_str());
            return;
        }
        std::printf("%-24s total=%zu\n", q, static_cast<std::size_t>(res->total_hits));
        for (const auto& h : res->hits)
            std::printf("    %s   score=%.4f\n", h.id.c_str(), h.score);
    };

    run("inverted index", "body");
    run("bm25", "body");
    run("title:tokenizer", "title");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return (*writer)->Close().ok() ? 0 : 1;
}
