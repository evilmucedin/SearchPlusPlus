// hello_search — minimal end-to-end use of the SearchPlusPlus C++ library.
//
// Indexes a tiny in-memory corpus, refreshes, runs a query, prints the top
// hits with their BM25 scores. Demonstrates the four headers a user of this
// library will almost always reach for:
//
//   spp/index/schema.h       — declare field types up front
//   spp/index/index_writer.h — open an index, AddDocument, Refresh
//   spp/query/query_parser.h — parse a query string into an AST
//   spp/query/searcher.h     — run the AST against a reader
//
// Build (from the repo root, with examples enabled in CMake):
//   cmake --preset default -DSPP_BUILD_EXAMPLES=ON
//   cmake --build --preset default --target hello_search
//   ./build/default/examples/hello_search/hello_search
//
// The example uses a fresh temp directory each run so it doesn't depend on
// any persistent state.

#include "spp/base/expected.h"
#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

struct Doc {
    std::string id;
    std::string title;
    std::string body;
};

// Tiny corpus chosen so several queries return different rankings.
const std::vector<Doc> kCorpus = {
    {"a",
     "Search engines explained",
     "An overview of how full-text search engines tokenize, index, and rank."},
    {"b",
     "Inverted index basics",
     "An inverted index maps each term to the list of documents that contain it."},
    {"c",
     "BM25 in plain English",
     "BM25 scores how relevant a document is to a query based on term frequency and document "
     "length."},
    {"d",
     "Postings and skip lists",
     "Posting lists store doc ids and term frequencies; skip lists speed up intersection."},
    {"e",
     "What is a tokenizer",
     "A tokenizer splits text into the tokens that the index actually stores."},
};

// Pick a fresh temp directory so successive runs don't share state.
std::filesystem::path MakeTempIndexDir() {
    auto base = std::filesystem::temp_directory_path();
    std::random_device rd;
    auto suffix = std::to_string(rd()) + "_" + std::to_string(rd());
    auto dir = base / ("spp_hello_" + suffix);
    std::filesystem::create_directories(dir);
    return dir;
}

// Build a minimal stored-source JSON for AddDocument. The index keeps this
// verbatim so it can be returned in hit responses — we don't need a real
// JSON serializer for that.
std::string MakeSourceJson(const Doc& d) {
    auto esc = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '"' || c == '\\')
                out.push_back('\\');
            out.push_back(c);
        }
        return out;
    };
    return std::string(R"({"_id":")") + esc(d.id) + R"(","title":")" + esc(d.title) +
           R"(","body":")" + esc(d.body) + R"("})";
}

int Die(const char* what, const spp::Status& s) {
    std::fprintf(stderr, "hello_search: %s: %s\n", what, s.message().c_str());
    return 1;
}

}  // namespace

int main() {
    const auto index_dir = MakeTempIndexDir();
    std::fprintf(stderr, "hello_search: indexing into %s\n", index_dir.c_str());

    // 1. Declare the schema. _id is implicit (always Keyword, always stored).
    //    Both content fields use the default "standard" analyzer (lowercase,
    //    Unicode-aware tokenization, English stop-word filter).
    spp::index::Schema schema;
    if (auto st = schema.AddField({.name = "title", .type = spp::index::FieldType::kText});
        !st.ok())
        return Die("AddField title", st);
    if (auto st = schema.AddField({.name = "body", .type = spp::index::FieldType::kText}); !st.ok())
        return Die("AddField body", st);

    // 2. Open the index with that schema.
    spp::index::IndexOpenOptions opts;
    opts.initial_schema = &schema;
    auto writer_e = spp::index::IndexWriter::Open(index_dir.string(), opts);
    if (!writer_e.ok())
        return Die("IndexWriter::Open", writer_e.status());
    auto& writer = *writer_e;

    // 3. Add documents. The second argument is the stored source — we pass
    //    a JSON string so it can be echoed back in hits later if needed.
    for (const auto& d : kCorpus) {
        spp::index::Document doc;
        doc.id = d.id;
        doc.fields["title"] = d.title;
        doc.fields["body"] = d.body;
        if (auto st = writer->AddDocument(doc, MakeSourceJson(d)); !st.ok())
            return Die("AddDocument", st);
    }

    // 4. Refresh seals the in-memory segment so it becomes visible to readers.
    auto gen_e = writer->Refresh();
    if (!gen_e.ok())
        return Die("Refresh", gen_e.status());

    // 5. Run a couple of queries against a fresh reader.
    auto reader = writer->CurrentReader();
    auto run_query = [&](const std::string& q, const std::string& default_field) {
        auto ast_e = spp::query::Parse(q, default_field);
        if (!ast_e.ok()) {
            std::fprintf(stderr, "  parse error: %s\n", ast_e.status().message().c_str());
            return;
        }
        spp::query::Searcher s(reader);
        spp::query::SearchOptions sopts;
        sopts.size = 5;
        sopts.default_field = default_field;
        auto res_e = s.Search(*ast_e, sopts);
        if (!res_e.ok()) {
            std::fprintf(stderr, "  search error: %s\n", res_e.status().message().c_str());
            return;
        }
        std::printf("query=%-40s total=%zu\n",
                    ("\"" + q + "\"").c_str(),
                    static_cast<std::size_t>(res_e->total_hits));
        for (const auto& h : res_e->hits) {
            std::printf("    %s   score=%.4f\n", h.id.c_str(), h.score);
        }
    };

    std::puts("\n--- queries ---");
    run_query("inverted index", "body");
    run_query("bm25", "body");
    run_query("title:tokenizer", "title");
    run_query("posting AND skip", "body");

    // 6. Close is required for a clean shutdown (flushes the manifest).
    if (auto st = writer->Close(); !st.ok())
        return Die("Close", st);

    // Clean up the temp index — for a long-lived program you'd of course
    // skip this and reopen the index on the next run.
    std::error_code ec;
    std::filesystem::remove_all(index_dir, ec);
    return 0;
}
