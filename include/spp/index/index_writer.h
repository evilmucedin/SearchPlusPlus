#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/base/types.h"
#include "spp/index/document.h"
#include "spp/index/index_reader.h"
#include "spp/index/manifest.h"
#include "spp/index/mutable_segment.h"
#include "spp/index/schema.h"
#include "spp/index/translog.h"
#include "spp/store/directory.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace spp::index {

// Options for opening an existing or newly initialized index directory.
struct IndexOpenOptions {
    // If non-null, used as the schema when the directory has no schema yet.
    // Ignored if the directory already has a schema.json.
    const Schema* initial_schema = nullptr;

    // Name of the schema file inside the index directory.
    std::string schema_filename = "schema.json";
};

// Single-writer, NRT (near-real-time) index writer.
//
// Concurrency contract: AddDocument and Refresh are mutually exclusive, both
// internally serialized by a single mutex. CurrentReader() is lock-free for
// readers — it loads the published `shared_ptr<const IndexReader>` snapshot.
class IndexWriter {
 public:
    static Expected<std::unique_ptr<IndexWriter>> Open(const std::filesystem::path& dir_path,
                                                       const IndexOpenOptions& opts = {});

    ~IndexWriter();
    IndexWriter(const IndexWriter&) = delete;
    IndexWriter& operator=(const IndexWriter&) = delete;

    // Add (or "index") a document. The doc must include all required fields.
    // The translog is fsynced before returning; the doc is not visible to searches
    // until Refresh() is called.
    //
    // `raw_json` is the canonical JSON payload to write to the translog. If empty,
    // the writer serializes a minimal payload from `doc`.
    Status AddDocument(const Document& doc, std::string_view raw_json = {});

    // Seal the in-memory segment to disk (if non-empty), publish a new IndexReader,
    // truncate the translog. Returns the new generation.
    Expected<Generation> Refresh();

    // Refresh if any pending docs; then close translog. Subsequent ops fail.
    Status Close();

    // Returns the latest published reader. Lock-free.
    std::shared_ptr<const IndexReader> CurrentReader() const;

    const Schema& schema() const noexcept {
        return *schema_;
    }
    Generation generation() const noexcept {
        return current_gen_.load(std::memory_order_acquire);
    }

 private:
    IndexWriter() = default;
    Status BootstrapInitial(const IndexOpenOptions& opts);
    Status ReplayTranslog();
    // mu_ MUST be held by the caller (or no other thread can access this object yet).
    void PublishReaderLocked(std::shared_ptr<const IndexReader> r);
    Expected<std::shared_ptr<const SegmentReader>> SealCurrentSegment();

    std::filesystem::path dir_path_;
    std::unique_ptr<store::Directory> dir_;
    std::shared_ptr<const Schema> schema_;
    std::unique_ptr<MutableSegment> mutable_segment_;
    std::unique_ptr<Translog> translog_;

    std::vector<std::shared_ptr<const SegmentReader>> sealed_segments_;
    std::atomic<Generation> current_gen_{0};
    std::atomic<std::uint32_t> next_segment_id_{0};

    mutable std::mutex mu_;
    std::shared_ptr<const IndexReader> published_reader_;  // guarded by mu_
};

}  // namespace spp::index
