#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/json/json_value.h"
#include "spp/query/features.h"

#include <array>
#include <memory>
#include <string>

namespace spp::query {

// A ranker scores a candidate doc from its FeatureVector. Higher scores rank
// higher. Rankers are immutable after construction; SearchServer publishes
// them via atomic shared_ptr so search threads pick them up lock-free.
class Ranker {
 public:
    virtual ~Ranker() = default;
    virtual float Score(const FeatureVector& fv) const = 0;
    virtual const char* TypeName() const noexcept = 0;
    // For GET /_ltr response.
    virtual spp::json::JsonValue ToJson() const = 0;
};

// Pass-through ranker: returns the BM25 total. Semantically equivalent to v0.1
// scoring, useful as a default when no model is loaded.
class Bm25Ranker final : public Ranker {
 public:
    float Score(const FeatureVector& fv) const override;
    const char* TypeName() const noexcept override {
        return "bm25";
    }
    spp::json::JsonValue ToJson() const override;
};

// Linear ranker: dot(weights, features) + bias. Cheap, hand-tunable via PUT,
// and lets us validate the two-stage Searcher end-to-end without CatBoost.
class LinearRanker final : public Ranker {
 public:
    LinearRanker() = default;
    LinearRanker(float bias, const std::array<float, kFeatureCount>& weights);

    static Expected<std::unique_ptr<LinearRanker>> FromJson(const spp::json::JsonValue& v);

    float Score(const FeatureVector& fv) const override;
    const char* TypeName() const noexcept override {
        return "linear";
    }
    spp::json::JsonValue ToJson() const override;

    float bias() const noexcept {
        return bias_;
    }
    const std::array<float, kFeatureCount>& weights() const noexcept {
        return weights_;
    }

 private:
    float bias_ = 0.0f;
    std::array<float, kFeatureCount> weights_{};
};

// CatBoost ranker: invokes the apply_catboost_model symbol that the embedded
// `save_model(format='cpp')` export defines. The stub model (models/catboost_model.cpp)
// returns 0; replace it with a real export and rebuild to deploy a trained model.
//
// When SPP_WITH_LTR_MODEL is OFF the stub is still linked, so this class always
// compiles — the caller is responsible for deciding whether the active model is
// the trivial stub or a trained one.
class CatboostRanker final : public Ranker {
 public:
    float Score(const FeatureVector& fv) const override;
    const char* TypeName() const noexcept override {
        return "catboost";
    }
    spp::json::JsonValue ToJson() const override;
};

}  // namespace spp::query
