#include "spp/query/iterators.h"

#include "spp/base/types.h"
#include "spp/store/varbyte.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace spp::query {

namespace {

DocId DecodeNext(
    const char*& cursor, const char* end, DocId prev, std::uint32_t& freq_out, bool first) {
    if (cursor >= end)
        return kNoMoreDocs;
    auto delta = store::DecodeVarUint32(cursor, end);
    if (!delta.ok())
        return kNoMoreDocs;
    auto tf = store::DecodeVarUint32(cursor, end);
    if (!tf.ok())
        return kNoMoreDocs;
    freq_out = *tf;
    return first ? *delta : prev + *delta;
}

}  // namespace

// ---- TermIterator -------------------------------------------------------------------

TermIterator::TermIterator(std::string_view posting_bytes, std::uint32_t df, std::uint64_t total_tf)
    : bytes_(posting_bytes),
      cursor_(posting_bytes.data()),
      end_(posting_bytes.data() + posting_bytes.size()),
      remaining_(0),
      df_(df),
      total_tf_(total_tf) {
    auto count = store::DecodeVarUint32(cursor_, end_);
    if (count.ok())
        remaining_ = *count;
    if (remaining_ != df_) {
        // Defensive — bytes say something different than the dictionary metadata.
        df_ = remaining_;
    }
}

DocId TermIterator::Next() {
    if (remaining_ == 0) {
        current_ = kNoMoreDocs;
        return current_;
    }
    DocId d = DecodeNext(cursor_, end_, prev_, current_freq_, !started_);
    if (d == kNoMoreDocs) {
        current_ = kNoMoreDocs;
        return current_;
    }
    started_ = true;
    prev_ = d;
    --remaining_;
    current_ = d;
    return current_;
}

DocId TermIterator::Advance(DocId target) {
    while (current_ < target || !started_) {
        const DocId d = Next();
        if (d == kNoMoreDocs)
            return kNoMoreDocs;
        if (d >= target)
            return d;
    }
    return current_;
}

// ---- ConjunctionIterator ------------------------------------------------------------

ConjunctionIterator::ConjunctionIterator(std::vector<std::unique_ptr<DocIdSetIterator>> children)
    : children_(std::move(children)) {
    // Cheapest-first.
    std::sort(children_.begin(),
              children_.end(),
              [](const std::unique_ptr<DocIdSetIterator>& a,
                 const std::unique_ptr<DocIdSetIterator>& b) { return a->Cost() < b->Cost(); });
    // Prime each child to its first doc id; if any is empty, the whole AND is empty.
    for (auto& c : children_) {
        if (c->Next() == kNoMoreDocs) {
            current_ = kNoMoreDocs;
            return;
        }
    }
}

DocId ConjunctionIterator::Next() {
    if (children_.empty()) {
        current_ = kNoMoreDocs;
        return current_;
    }
    // Advance the lead iterator past the current doc, then leapfrog all the others.
    DocId target;
    if (current_ == kNoMoreDocs) {
        target = children_.front()->DocId_();
    } else {
        target = children_.front()->Next();
        if (target == kNoMoreDocs) {
            current_ = kNoMoreDocs;
            return current_;
        }
    }

    while (true) {
        bool all_match = true;
        DocId candidate = target;
        for (std::size_t i = 1; i < children_.size(); ++i) {
            const DocId d = children_[i]->Advance(candidate);
            if (d == kNoMoreDocs) {
                current_ = kNoMoreDocs;
                return current_;
            }
            if (d != candidate) {
                all_match = false;
                candidate = d;
                break;
            }
        }
        if (all_match) {
            current_ = candidate;
            total_freq_ = 0;
            for (auto& c : children_)
                total_freq_ += c->Freq();
            return current_;
        }
        const DocId d0 = children_.front()->Advance(candidate);
        if (d0 == kNoMoreDocs) {
            current_ = kNoMoreDocs;
            return current_;
        }
        target = d0;
    }
}

DocId ConjunctionIterator::Advance(DocId target) {
    while (current_ < target || current_ == kNoMoreDocs) {
        if (children_.empty())
            return kNoMoreDocs;
        const DocId d = children_.front()->Advance(target);
        if (d == kNoMoreDocs) {
            current_ = kNoMoreDocs;
            return current_;
        }
        DocId candidate = d;
        bool all_match = true;
        for (std::size_t i = 1; i < children_.size(); ++i) {
            const DocId dd = children_[i]->Advance(candidate);
            if (dd == kNoMoreDocs) {
                current_ = kNoMoreDocs;
                return current_;
            }
            if (dd != candidate) {
                all_match = false;
                target = dd;
                break;
            }
        }
        if (all_match) {
            current_ = candidate;
            total_freq_ = 0;
            for (auto& c : children_)
                total_freq_ += c->Freq();
            return current_;
        }
    }
    return current_;
}

std::int64_t ConjunctionIterator::Cost() const {
    if (children_.empty())
        return 0;
    return children_.front()->Cost();
}

// ---- DisjunctionIterator ------------------------------------------------------------

DisjunctionIterator::DisjunctionIterator(std::vector<std::unique_ptr<DocIdSetIterator>> children)
    : children_(std::move(children)) {}

DocId DisjunctionIterator::NextAfter(DocId after) {
    // Advance every child that is at-or-before `after` (or whose iteration hasn't started).
    for (auto& c : children_) {
        const DocId d = c->DocId_();
        if (d == kNoMoreDocs)
            continue;
        // Before any Next() the iterator's DocId_() is kNoMoreDocs (see TermIterator),
        // so iteration is started by the first NextAfter(kNoMoreDocs).
        if (after == kNoMoreDocs)
            continue;  // initial seek: don't re-advance positioned children
        if (d <= after)
            (void)c->Next();
    }

    DocId best = kNoMoreDocs;
    total_freq_ = 0;
    bool first = true;
    for (auto& c : children_) {
        const DocId d = c->DocId_();
        if (d == kNoMoreDocs)
            continue;
        if (after != kNoMoreDocs && d <= after)
            continue;
        if (first || d < best) {
            best = d;
            first = false;
        }
    }
    if (best == kNoMoreDocs)
        return best;
    for (auto& c : children_) {
        if (c->DocId_() == best)
            total_freq_ += c->Freq();
    }
    return best;
}

DocId DisjunctionIterator::Next() {
    if (current_ == kNoMoreDocs) {
        // Are we still un-started? Prime every child once and pick the smallest.
        bool any_unstarted = false;
        for (const auto& c : children_) {
            if (c->DocId_() == kNoMoreDocs) {
                any_unstarted = true;
                break;
            }
        }
        if (any_unstarted) {
            for (auto& c : children_) {
                if (c->DocId_() == kNoMoreDocs)
                    (void)c->Next();
            }
            DocId best = kNoMoreDocs;
            bool first = true;
            for (auto& c : children_) {
                const DocId d = c->DocId_();
                if (d == kNoMoreDocs)
                    continue;
                if (first || d < best) {
                    best = d;
                    first = false;
                }
            }
            current_ = best;
            total_freq_ = 0;
            if (current_ != kNoMoreDocs) {
                for (auto& c : children_) {
                    if (c->DocId_() == current_)
                        total_freq_ += c->Freq();
                }
            }
            return current_;
        }
        return current_;
    }
    current_ = NextAfter(current_);
    return current_;
}

DocId DisjunctionIterator::Advance(DocId target) {
    for (auto& c : children_) {
        if (c->DocId_() != kNoMoreDocs && c->DocId_() < target)
            (void)c->Advance(target);
    }
    DocId best = kNoMoreDocs;
    bool first = true;
    for (auto& c : children_) {
        const DocId d = c->DocId_();
        if (d == kNoMoreDocs || d < target)
            continue;
        if (first || d < best) {
            best = d;
            first = false;
        }
    }
    current_ = best;
    total_freq_ = 0;
    if (current_ != kNoMoreDocs) {
        for (auto& c : children_) {
            if (c->DocId_() == current_)
                total_freq_ += c->Freq();
        }
    }
    return current_;
}

std::int64_t DisjunctionIterator::Cost() const {
    std::int64_t sum = 0;
    for (const auto& c : children_)
        sum += c->Cost();
    return sum;
}

// ---- MatchAllIterator ---------------------------------------------------------------

DocId MatchAllIterator::Next() {
    if (!started_) {
        started_ = true;
        if (doc_count_ == 0) {
            current_ = kNoMoreDocs;
            return current_;
        }
        current_ = 0;
        return current_;
    }
    if (current_ + 1 >= doc_count_) {
        current_ = kNoMoreDocs;
        return current_;
    }
    ++current_;
    return current_;
}

DocId MatchAllIterator::Advance(DocId target) {
    if (target >= doc_count_) {
        current_ = kNoMoreDocs;
        return current_;
    }
    started_ = true;
    current_ = target;
    return current_;
}

}  // namespace spp::query
