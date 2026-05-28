#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace spp::index {

struct Document {
    std::string id;
    std::map<std::string, std::string> fields;  // field name → text

    // v0.2 LTR signals. Both are optional and ignored when the schema does not
    // declare the corresponding storage:
    //   - field_token_weights["body"] is aligned with the analyzer tokens of
    //     the "body" field. Missing entries default to 1.0; extra entries are
    //     dropped silently.
    //   - doc_quality is a static per-doc score in [0, 1] (no clamping; the
    //     ranker can scale it). Stored in the .dvq stripe when Schema enables
    //     store_doc_quality.
    std::map<std::string, std::vector<float>> field_token_weights;
    std::optional<float> doc_quality;
};

}  // namespace spp::index
