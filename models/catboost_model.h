#pragma once

#include <string>
#include <vector>

// Single-symbol contract used by spp::query::CatboostRanker. CatBoost's Python
// `save_model(filename, format='cpp')` generates a function with exactly this
// signature; the default `models/catboost_model.cpp` is a stub that returns 0
// so the engine builds without a trained model. Drop a real export over the
// default file and rebuild to deploy.
//
// floatFeatures: ordered to match spp::query::FeatureVector (see features.h).
// catFeatures: empty in v0.2 — we have no categorical features yet.
double apply_catboost_model(const std::vector<float>& floatFeatures,
                            const std::vector<std::string>& catFeatures);
