#pragma once

#include "spp/base/expected.h"
#include "spp/base/types.h"
#include "spp/json/json_value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace spp::index {

enum class FieldType : std::uint8_t {
    kText,     // runs the field's analyzer at index time and query time
    kKeyword,  // indexed as one token unchanged (used for _id)
};

struct FieldMapping {
    std::string name;
    FieldType type = FieldType::kText;
    std::string analyzer = "standard";  // ignored for kKeyword
    bool stored = false;                // included in stored-fields and returned in hits
};

class Schema {
 public:
    Schema() = default;

    // The _id field is always present, always Keyword, always stored.
    static constexpr std::string_view kIdField = "_id";

    // Add a field. The order of adds defines the field id.
    Status AddField(FieldMapping mapping);

    const std::vector<FieldMapping>& fields() const noexcept {
        return fields_;
    }
    std::size_t field_count() const noexcept {
        return fields_.size();
    }

    bool HasField(std::string_view name) const noexcept;
    FieldId GetFieldId(std::string_view name) const;  // returns kInvalidFieldId if missing
    const FieldMapping& GetField(FieldId id) const {
        return fields_[id];
    }

    // JSON round-trip.
    spp::json::JsonValue ToJson() const;
    static Expected<Schema> FromJson(const spp::json::JsonValue& v);

    // Convenience: build a Schema from a mappings JSON of the form
    //   { "field_a": { "type": "text" }, "field_b": { "type": "keyword", "stored": true } }
    // The _id field is automatically prepended if not present.
    static Expected<Schema> FromMappingsJson(const spp::json::JsonValue& mappings);

 private:
    std::vector<FieldMapping> fields_;
    std::unordered_map<std::string, FieldId> by_name_;
};

const char* FieldTypeName(FieldType t) noexcept;
Expected<FieldType> ParseFieldType(std::string_view name);

}  // namespace spp::index
