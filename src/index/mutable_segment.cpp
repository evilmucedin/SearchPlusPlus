#include "spp/index/mutable_segment.h"

#include "spp/analyze/analyzer.h"
#include "spp/json/json_serializer.h"
#include "spp/json/json_value.h"

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

        // Aggregate per-term TF in this doc.
        std::unordered_map<std::string, std::uint32_t> tf;
        tf.reserve(tokens.size());
        for (const auto& t : tokens)
            ++tf[t.text];

        MutableFieldData& fd = field_data_[fi];
        for (auto& [term, count] : tf) {
            auto& pl = fd.postings[term];
            pl.push_back(Posting{id, count});
            approx_bytes_ += term.size() + 8;
        }
        fd.sum_field_length += tokens.size();
        ++fd.doc_count;
    }

    stored_blobs_.push_back(spp::json::Serialize(spp::json::JsonValue{std::move(stored)}));
    doc_ids_.push_back(doc.id);
    ++next_doc_id_;
    return id;
}

}  // namespace spp::index
