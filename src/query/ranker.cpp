#include "spp/query/ranker.h"

#include "spp/json/json_value.h"
#include "spp/query/features.h"

#include "models/catboost_model.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace spp::query {

// ---- Bm25Ranker -------------------------------------------------------------

float Bm25Ranker::Score(const FeatureVector& fv) const {
    return fv[static_cast<std::size_t>(Feature::kBm25Total)];
}

spp::json::JsonValue Bm25Ranker::ToJson() const {
    spp::json::JsonObject o;
    o["type"] = std::string{"bm25"};
    return spp::json::JsonValue{std::move(o)};
}

// ---- LinearRanker -----------------------------------------------------------

LinearRanker::LinearRanker(float bias, const std::array<float, kFeatureCount>& weights)
    : bias_(bias), weights_(weights) {}

float LinearRanker::Score(const FeatureVector& fv) const {
    double s = static_cast<double>(bias_);
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        s += static_cast<double>(weights_[i]) * static_cast<double>(fv[i]);
    }
    return static_cast<float>(s);
}

spp::json::JsonValue LinearRanker::ToJson() const {
    spp::json::JsonObject o;
    o["type"] = std::string{"linear"};
    o["bias"] = static_cast<double>(bias_);
    spp::json::JsonArray w;
    w.reserve(kFeatureCount);
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        w.push_back(spp::json::JsonValue{static_cast<double>(weights_[i])});
    }
    o["weights"] = spp::json::JsonValue{std::move(w)};
    spp::json::JsonArray names;
    names.reserve(kFeatureCount);
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        names.push_back(
            spp::json::JsonValue{std::string{FeatureName(static_cast<Feature>(i))}});
    }
    o["feature_names"] = spp::json::JsonValue{std::move(names)};
    return spp::json::JsonValue{std::move(o)};
}

Expected<std::unique_ptr<LinearRanker>> LinearRanker::FromJson(const spp::json::JsonValue& v) {
    if (!v.is_object())
        return Status::InvalidArgument("linear ranker: expected object");
    auto out = std::make_unique<LinearRanker>();
    if (const auto* b = v.find("bias"); b != nullptr && b->is_number()) {
        out->bias_ = static_cast<float>(b->as_double());
    }
    const auto* w = v.find("weights");
    if (w == nullptr || !w->is_array()) {
        return Status::InvalidArgument("linear ranker: missing 'weights' array");
    }
    const auto& arr = w->as_array();
    if (arr.size() != kFeatureCount) {
        return Status::InvalidArgument(
            "linear ranker: 'weights' length must equal feature count");
    }
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        if (!arr[i].is_number())
            return Status::InvalidArgument("linear ranker: non-numeric weight");
        out->weights_[i] = static_cast<float>(arr[i].as_double());
    }
    return out;
}

// ---- CatboostRanker ---------------------------------------------------------

float CatboostRanker::Score(const FeatureVector& fv) const {
    std::vector<float> floats;
    floats.reserve(kFeatureCount);
    for (std::size_t i = 0; i < kFeatureCount; ++i)
        floats.push_back(fv[i]);
    std::vector<std::string> cats;
    return static_cast<float>(apply_catboost_model(floats, cats));
}

spp::json::JsonValue CatboostRanker::ToJson() const {
    spp::json::JsonObject o;
    o["type"] = std::string{"catboost"};
    o["feature_schema_version"] = static_cast<std::int64_t>(kFeatureSchemaVersion);
    return spp::json::JsonValue{std::move(o)};
}

}  // namespace spp::query
