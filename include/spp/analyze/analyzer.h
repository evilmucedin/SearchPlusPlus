#pragma once

#include "spp/analyze/token.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace spp::analyze {

class Analyzer {
 public:
    virtual ~Analyzer() = default;

    // Tokenize `input`. Caller-supplied `out` is cleared first.
    virtual void Analyze(std::string_view input, std::vector<Token>& out) const = 0;

    // Stable fingerprint persisted to the segment metadata. Two analyzers
    // with the same fingerprint MUST produce the same tokens for any input.
    virtual std::string Fingerprint() const = 0;
};

// Create the default v0.1 analyzer: ASCII English (lowercase + stop words).
std::unique_ptr<Analyzer> MakeStandardAnalyzer();

// Create a "keyword" analyzer that emits the input as a single token, unchanged.
// Used for ID and other exact-match fields.
std::unique_ptr<Analyzer> MakeKeywordAnalyzer();

}  // namespace spp::analyze
