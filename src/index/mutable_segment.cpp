#include "spp/index/mutable_segment.h"

#include "spp/analyze/analyzer.h"
#include "spp/json/json_serializer.h"
#include "spp/json/json_value.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spp::index {

std::unique_ptr<analyze::Analyzer> MutableSegment::AnalyzerForField(const FieldMapping& f) {
    if (f.type == FieldType::kKeyword)
        return analyze::MakeKeywordAnalyzer();
    // For text fields the only analyzer name v0.1 understands is "standard".
    return analyze::MakeStandardAnalyzer();
}

MutableSegment::MutableSegment(const Schema& schema) : schema_(schema) {
    const std::size_t n = schema_.field_count();
    analyzers_.reserve(n);
    field_data_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        analyzers_.push_back(AnalyzerForField(schema_.fields()[i]));
    }
}

namespace {

struct PerDocTermStats {
    std::uint32_t tf = 0;
    std::uint16_t first_pos = 0;
    float max_weight = 0.0f;
};

}  // namespace

Expected<DocId> MutableSegment::AddDocument(const Document& doc) {
    if (doc.id.empty())
        return Status::InvalidArgument("document _id is empty");
    if (next_doc_id_ == kNoMoreDocs)
        return Status::OutOfRange("doc id space exhausted");

    const DocId id = next_doc_id_;

    // Collect stored fields for this doc. Always store _id.
    spp::json::JsonObject stored;
    stored[std::string{Schema::kIdField}] = doc.id;

    std::vector<analyze::Token> tokens;
    for (std::size_t fi = 0; fi < schema_.fields().size(); ++fi) {
        const FieldMapping& f = schema_.fields()[fi];
        const std::string* value = nullptr;
        if (f.name == Schema::kIdField) {
            value = &doc.id;
        } else {
            auto it = doc.fields.find(f.name);
            if (it != doc.fields.end())
                value = &it->second;
        }
        if (value == nullptr)
            continue;

        if (f.stored && f.name != Schema::kIdField) {
            stored[f.name] = *value;
        }

        analyzers_[fi]->Analyze(*value, tokens);
        if (tokens.empty())
            continue;

        // Caller-supplied per-token weights, aligned with analyzer output. Missing
        // entries default to 1.0; extras are ignored. Only consulted when the field
        // opted into store_token_weights — otherwise we skip the lookup.
        const std::vector<float>* weights = nullptr;
        if (f.store_token_weights) {
            auto wit = doc.field_token_weights.find(f.name);
            if (wit != doc.field_token_weights.end())
                weights = &wit->second;
        }

        // First pass over tokens captures tf, first position, and max weight per term.
        std::unordered_map<std::string, PerDocTermStats> stats;
        stats.reserve(tokens.size());
        constexpr std::uint16_t kMaxPos = std::numeric_limits<std::uint16_t>::max();
        for (std::size_t pi = 0; pi < tokens.size(); ++pi) {
            const auto& t = tokens[pi];
            auto& st = stats[t.text];
            if (st.tf == 0) {
                st.first_pos = pi < kMaxPos ? static_cast<std::uint16_t>(pi) : kMaxPos;
            }
            ++st.tf;
            if (weights != nullptr) {
                const float w = pi < weights->size() ? (*weights)[pi] : 1.0f;
                if (w > st.max_weight)
                    st.max_weight = w;
            }
        }

        MutableFieldData& fd = field_data_[fi];
        for (auto& [term, st] : stats) {
            auto& pl = fd.postings[term];
            Posting p;
            p.doc_id = id;
            p.tf = st.tf;
            p.first_pos = st.first_pos;
            p.max_token_weight = weights != nullptr ? st.max_weight : 1.0f;
            pl.push_back(p);
            approx_bytes_ += term.size() + sizeof(Posting);
        }
        fd.sum_field_length += tokens.size();
        ++fd.doc_count;
    }

    stored_blobs_.push_back(spp::json::Serialize(spp::json::JsonValue{std::move(stored)}));
    doc_ids_.push_back(doc.id);
    if (schema_.store_doc_quality()) {
        doc_quality_.push_back(doc.doc_quality.value_or(0.0f));
    }
    ++next_doc_id_;
    return id;
}

}  // namespace spp::index
