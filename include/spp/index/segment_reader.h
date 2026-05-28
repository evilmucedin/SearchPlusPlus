#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/base/types.h"
#include "spp/index/segment_info.h"
#include "spp/store/directory.h"
#include "spp/store/file_io.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace spp::index {

// One term's metadata in the in-memory term dictionary.
struct TermEntry {
    std::string term;
    std::uint32_t df = 0;
    std::uint64_t total_tf = 0;
    std::uint64_t postings_offset = 0;  // offset into the field's .doc range
    std::uint64_t postings_bytes = 0;
};

// Per-field state inside an opened segment.
struct FieldRead {
    FieldStats stats;
    std::vector<TermEntry> terms;  // sorted by term bytes; binary-searchable
};

// Read-only view of a sealed segment. All data is loaded into memory in v0.1.
class SegmentReader {
 public:
    static Expected<std::unique_ptr<SegmentReader>> Open(store::Directory& dir, std::string stem);

    const SegmentInfo& info() const noexcept {
        return info_;
    }
    const std::string& stem() const noexcept {
        return info_.stem;
    }
    DocId doc_count() const noexcept {
        return info_.doc_count;
    }

    // Returns nullptr if the field id is out of range.
    const FieldRead* field(FieldId field_id) const;
    // Returns kInvalidFieldId if the field name is not present.
    FieldId GetFieldId(std::string_view name) const;

    // Look up a term in a field. Returns nullptr if not present.
    const TermEntry* FindTerm(FieldId field_id, std::string_view term) const;

    // Returns the raw bytes of a posting list (starting from the df varuint).
    std::string_view PostingBytes(FieldId field_id, const TermEntry& te) const;

    // Read the stored-fields JSON for a doc id. Returns empty view if missing.
    std::string_view StoredFields(DocId id) const;

    // v0.2: per-doc static quality from the `.dvq` stripe. Returns 0.0 if the
    // segment was sealed without a quality stripe or the id is out of range.
    float DocQuality(DocId id) const;
    bool has_doc_quality() const noexcept {
        return !doc_quality_.empty();
    }

 private:
    SegmentReader() = default;

    SegmentInfo info_;
    std::vector<FieldRead> fields_;
    std::string doc_buf_;
    std::string fdt_buf_;
    std::vector<std::uint64_t> fdx_offsets_;
    std::vector<float> doc_quality_;
};

}  // namespace spp::index
