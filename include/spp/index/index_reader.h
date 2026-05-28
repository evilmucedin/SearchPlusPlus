#pragma once

#include "spp/base/types.h"
#include "spp/index/schema.h"
#include "spp/index/segment_reader.h"

#include <memory>
#include <vector>

namespace spp::index {

// An immutable snapshot of the index at a point in time. Held by `shared_ptr<const>`,
// safe to share across threads. Created by `IndexWriter::CurrentReader()`.
class IndexReader {
 public:
    IndexReader(std::shared_ptr<const Schema> schema,
                std::vector<std::shared_ptr<const SegmentReader>> segments)
        : schema_(std::move(schema)), segments_(std::move(segments)) {}

    const Schema& schema() const noexcept {
        return *schema_;
    }

    const std::vector<std::shared_ptr<const SegmentReader>>& segments() const noexcept {
        return segments_;
    }

    std::size_t segment_count() const noexcept {
        return segments_.size();
    }

    DocId total_doc_count() const noexcept {
        DocId total = 0;
        for (const auto& s : segments_)
            total += s->doc_count();
        return total;
    }

 private:
    std::shared_ptr<const Schema> schema_;
    std::vector<std::shared_ptr<const SegmentReader>> segments_;
};

}  // namespace spp::index
