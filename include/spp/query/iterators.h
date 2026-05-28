#pragma once

#include "spp/base/types.h"
#include "spp/index/segment_info.h"
#include "spp/index/segment_reader.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace spp::query {

class TermIterator;  // forward

// Doc-id set iterator interface used by every operator in the plan.
class DocIdSetIterator {
 public:
    virtual ~DocIdSetIterator() = default;
    virtual DocId Next() = 0;  // returns kNoMoreDocs at the end
    virtual DocId Advance(DocId target) = 0;
    virtual DocId DocId_() const = 0;       // current doc id; kNoMoreDocs before first Next/Advance
    virtual std::int64_t Cost() const = 0;  // approximate cost (for planner ordering)

    // For scoring: term frequency at the current doc, or 0 for non-leaf operators.
    virtual std::uint32_t Freq() const {
        return 0;
    }

    // Walks the operator tree and collects every leaf TermIterator. The
    // feature extractor needs per-leaf access to TF/position/weight; the
    // aggregated Freq() from Conjunction/Disjunction is enough for BM25 but
    // too coarse for LTR.
    virtual void CollectLeaves(std::vector<TermIterator*>&) {}
};

// Decodes the per-term posting list from a SegmentReader.
class TermIterator final : public DocIdSetIterator {
 public:
    // has_positions / has_token_weights describe the layout of the posting
    // bytes; both default to false so v0.1 callers compile unchanged.
    TermIterator(std::string_view posting_bytes,
                 std::uint32_t df,
                 std::uint64_t total_tf,
                 bool has_positions = false,
                 bool has_token_weights = false);

    DocId Next() override;
    DocId Advance(DocId target) override;
    DocId DocId_() const override {
        return current_;
    }
    std::int64_t Cost() const override {
        return df_;
    }
    std::uint32_t Freq() const override {
        return current_freq_;
    }
    void CollectLeaves(std::vector<TermIterator*>& out) override {
        out.push_back(this);
    }

    std::uint32_t df() const noexcept {
        return df_;
    }
    std::uint64_t total_tf() const noexcept {
        return total_tf_;
    }
    // Returns the first-occurrence position of this term in the current doc,
    // or spp::index::kNoPosition when positions are not stored.
    std::uint16_t Position() const noexcept {
        return current_pos_;
    }
    // Returns the per-(term, doc) token weight, or 1.0 when not stored.
    float TokenWeight() const noexcept {
        return current_weight_;
    }
    bool has_positions() const noexcept {
        return has_positions_;
    }
    bool has_token_weights() const noexcept {
        return has_token_weights_;
    }

 private:
    std::string_view bytes_;
    const char* cursor_;
    const char* end_;
    std::uint32_t remaining_;
    std::uint32_t df_;
    std::uint64_t total_tf_;
    bool has_positions_;
    bool has_token_weights_;
    DocId current_ = kNoMoreDocs;
    DocId prev_ = 0;
    std::uint32_t current_freq_ = 0;
    std::uint16_t current_pos_ = spp::index::kNoPosition;
    float current_weight_ = 1.0f;
    bool started_ = false;
};

// Logical AND: returns docs present in every child. Children are reordered by cost ascending.
class ConjunctionIterator final : public DocIdSetIterator {
 public:
    explicit ConjunctionIterator(std::vector<std::unique_ptr<DocIdSetIterator>> children);

    DocId Next() override;
    DocId Advance(DocId target) override;
    DocId DocId_() const override {
        return current_;
    }
    std::int64_t Cost() const override;
    std::uint32_t Freq() const override {
        return total_freq_;
    }
    void CollectLeaves(std::vector<TermIterator*>& out) override {
        for (auto& c : children_)
            c->CollectLeaves(out);
    }

 private:
    std::vector<std::unique_ptr<DocIdSetIterator>> children_;
    DocId current_ = kNoMoreDocs;
    std::uint32_t total_freq_ = 0;
};

// Logical OR: returns docs present in any child. Naive min-of-current-docids implementation.
class DisjunctionIterator final : public DocIdSetIterator {
 public:
    explicit DisjunctionIterator(std::vector<std::unique_ptr<DocIdSetIterator>> children);

    DocId Next() override;
    DocId Advance(DocId target) override;
    DocId DocId_() const override {
        return current_;
    }
    std::int64_t Cost() const override;
    std::uint32_t Freq() const override {
        return total_freq_;
    }
    void CollectLeaves(std::vector<TermIterator*>& out) override {
        for (auto& c : children_)
            c->CollectLeaves(out);
    }

 private:
    DocId NextAfter(DocId after);

    std::vector<std::unique_ptr<DocIdSetIterator>> children_;
    DocId current_ = kNoMoreDocs;
    std::uint32_t total_freq_ = 0;
};

// Iterates 0..doc_count-1. Used for MatchAll queries.
class MatchAllIterator final : public DocIdSetIterator {
 public:
    explicit MatchAllIterator(DocId doc_count) : doc_count_(doc_count) {}

    DocId Next() override;
    DocId Advance(DocId target) override;
    DocId DocId_() const override {
        return current_;
    }
    std::int64_t Cost() const override {
        return doc_count_;
    }

 private:
    DocId doc_count_;
    DocId current_ = kNoMoreDocs;
    bool started_ = false;
};

}  // namespace spp::query
