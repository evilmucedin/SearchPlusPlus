// Default CatBoost model stub. Replace this file with the output of CatBoost's
// `model.save_model('catboost_model.cpp', format='cpp')` and rebuild to deploy
// a trained model. The stub returns the BM25 total feature (slot 0) so that the
// "catboost" ranker mode degrades gracefully to BM25 when no real model is
// installed — useful for CI and smoke tests.

#include "models/catboost_model.h"

#include <vector>

double apply_catboost_model(const std::vector<float>& floatFeatures,
                            const std::vector<std::string>& /*catFeatures*/) {
    if (floatFeatures.empty())
        return 0.0;
    return static_cast<double>(floatFeatures[0]);
}
