#pragma once

#include "spp/analyze/analyzer.h"
#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/base/types.h"
#include "spp/index/document.h"
#include "spp/index/schema.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace spp::index {

// One posting buffered in memory while a segment is being built.
// first_pos / token_weight are populated only when the field's mapping enables
// the corresponding storage; the SegmentWriter is what consults the FieldMapping
// to decide which extra bytes to emit.
struct Posting {
    DocId doc_id;
    std::uint32_t tf;
    std::uint16_t first_pos = 0;    // ordinal of first occurrence in this doc's field
    float max_token_weight = 1.0f;  // max over all occurrences in this doc's field
};

// Per-(field, term) postings being accumulated.
using PostingList = std::vector<Posting>;

// Per-field collected state while we build a segment in memory.
struct MutableFieldData {
    // term-bytes → postings. Unordered while building; sorted at seal time.
    std::unordered_map<std::string, PostingList> postings;
    std::uint64_t sum_field_length = 0;  // sum of token counts across docs (for BM25 avgdl)
    std::uint32_t doc_count = 0;         // distinct docs that have any token in this field
};

// In-memory builder. Single-threaded by contract.
class MutableSegment {
 public:
    explicit MutableSegment(const Schema& schema);

    // Returns the local doc id assigned to this document.
    Expected<DocId> AddDocument(const Document& doc);

    DocId next_doc_id() const noexcept {
        return next_doc_id_;
    }
    DocId doc_count() const noexcept {
        return next_doc_id_;
    }
    std::uint64_t in_memory_bytes() const noexcept {
        return approx_bytes_;
    }

    const std::vector<MutableFieldData>& field_data() const noexcept {
        return field_data_;
    }
    const std::vector<std::string>& stored_field_blobs() const noexcept {
        return stored_blobs_;
    }
    const std::vector<std::string>& doc_ids() const noexcept {
        return doc_ids_;
    }
    const Schema& schema() const noexcept {
        return schema_;
    }
    // v0.2: per-doc static quality, parallel to doc_ids_. Populated only when
    // Schema::store_doc_quality() is true; otherwise empty.
    const std::vector<float>& doc_quality() const noexcept {
        return doc_quality_;
    }

 private:
    static std::unique_ptr<analyze::Analyzer> AnalyzerForField(const FieldMapping& f);

    const Schema& schema_;
    std::vector<std::unique_ptr<analyze::Analyzer>> analyzers_;
    std::vector<MutableFieldData> field_data_;
    std::vector<std::string> stored_blobs_;  // JSON-encoded per doc
    std::vector<std::string> doc_ids_;       // 1:1 with DocId
    std::vector<float> doc_quality_;         // 1:1 with DocId when schema enables it
    DocId next_doc_id_ = 0;
    std::uint64_t approx_bytes_ = 0;
};

}  // namespace spp::index
