#include "spp/index/schema.h"

#include <string>
#include <string_view>

namespace spp::index {

const char* FieldTypeName(FieldType t) noexcept {
    switch (t) {
        case FieldType::kText:
            return "text";
        case FieldType::kKeyword:
            return "keyword";
    }
    return "unknown";
}

Expected<FieldType> ParseFieldType(std::string_view name) {
    if (name == "text")
        return FieldType::kText;
    if (name == "keyword")
        return FieldType::kKeyword;
    return Status::InvalidArgument(std::string{"unknown field type: "} + std::string{name});
}

Status Schema::AddField(FieldMapping mapping) {
    if (mapping.name.empty())
        return Status::InvalidArgument("empty field name");
    if (by_name_.find(mapping.name) != by_name_.end()) {
        return Status::AlreadyExists("duplicate field: " + mapping.name);
    }
    if (fields_.size() >= kInvalidFieldId) {
        return Status::OutOfRange("too many fields");
    }
    const FieldId id = static_cast<FieldId>(fields_.size());
    by_name_[mapping.name] = id;
    fields_.push_back(std::move(mapping));
    return Status::Ok();
}

bool Schema::HasField(std::string_view name) const noexcept {
    return by_name_.find(std::string{name}) != by_name_.end();
}

FieldId Schema::GetFieldId(std::string_view name) const {
    auto it = by_name_.find(std::string{name});
    if (it == by_name_.end())
        return kInvalidFieldId;
    return it->second;
}

spp::json::JsonValue Schema::ToJson() const {
    spp::json::JsonArray arr;
    arr.reserve(fields_.size());
    for (const auto& f : fields_) {
        spp::json::JsonObject o;
        o["name"] = f.name;
        o["type"] = std::string{FieldTypeName(f.type)};
        o["analyzer"] = f.analyzer;
        o["stored"] = f.stored;
        o["boost"] = static_cast<double>(f.boost);
        o["position_decay"] = static_cast<double>(f.position_decay);
        o["store_token_weights"] = f.store_token_weights;
        arr.push_back(spp::json::JsonValue{std::move(o)});
    }
    spp::json::JsonObject root;
    root["fields"] = spp::json::JsonValue{std::move(arr)};
    root["store_doc_quality"] = store_doc_quality_;
    return spp::json::JsonValue{std::move(root)};
}

Expected<Schema> Schema::FromJson(const spp::json::JsonValue& v) {
    if (!v.is_object())
        return Status::InvalidArgument("schema: expected object");
    const auto* fields = v.find("fields");
    if (fields == nullptr || !fields->is_array()) {
        return Status::InvalidArgument("schema: missing 'fields' array");
    }
    Schema s;
    for (const auto& fv : fields->as_array()) {
        if (!fv.is_object())
            return Status::InvalidArgument("schema: field must be an object");
        FieldMapping m;
        if (const auto* n = fv.find("name"); n && n->is_string())
            m.name = n->as_string();
        else
            return Status::InvalidArgument("schema: field missing 'name'");
        if (const auto* t = fv.find("type"); t && t->is_string()) {
            auto ft = ParseFieldType(t->as_string());
            if (!ft.ok())
                return ft.status();
            m.type = *ft;
        }
        if (const auto* a = fv.find("analyzer"); a && a->is_string())
            m.analyzer = a->as_string();
        if (const auto* st = fv.find("stored"); st && st->is_bool())
            m.stored = st->as_bool();
        if (const auto* b = fv.find("boost"); b && b->is_number())
            m.boost = static_cast<float>(b->as_double());
        if (const auto* pd = fv.find("position_decay"); pd && pd->is_number())
            m.position_decay = static_cast<float>(pd->as_double());
        if (const auto* w = fv.find("store_token_weights"); w && w->is_bool())
            m.store_token_weights = w->as_bool();
        SPP_RETURN_IF_ERROR(s.AddField(std::move(m)));
    }
    if (!s.HasField(kIdField)) {
        return Status::InvalidArgument("schema must define an '_id' field");
    }
    if (const auto* q = v.find("store_doc_quality"); q && q->is_bool())
        s.store_doc_quality_ = q->as_bool();
    return s;
}

Expected<Schema> Schema::FromMappingsJson(const spp::json::JsonValue& mappings) {
    if (!mappings.is_object()) {
        return Status::InvalidArgument("mappings: expected object of field-name → field-config");
    }
    Schema s;
    // _id is always first if not present in the mappings.
    if (mappings.find(std::string{kIdField}) == nullptr) {
        FieldMapping idf;
        idf.name = std::string{kIdField};
        idf.type = FieldType::kKeyword;
        idf.stored = true;
        idf.analyzer = "keyword";
        SPP_RETURN_IF_ERROR(s.AddField(std::move(idf)));
    }
    for (const auto& [name, cfg] : mappings.as_object()) {
        if (!cfg.is_object()) {
            return Status::InvalidArgument("mappings: '" + name + "' must be an object");
        }
        FieldMapping m;
        m.name = name;
        if (const auto* t = cfg.find("type"); t && t->is_string()) {
            auto ft = ParseFieldType(t->as_string());
            if (!ft.ok())
                return ft.status();
            m.type = *ft;
        }
        if (m.type == FieldType::kKeyword)
            m.analyzer = "keyword";
        if (const auto* a = cfg.find("analyzer"); a && a->is_string())
            m.analyzer = a->as_string();
        if (const auto* st = cfg.find("stored"); st && st->is_bool())
            m.stored = st->as_bool();
        if (const auto* b = cfg.find("boost"); b && b->is_number())
            m.boost = static_cast<float>(b->as_double());
        if (const auto* pd = cfg.find("position_decay"); pd && pd->is_number())
            m.position_decay = static_cast<float>(pd->as_double());
        if (const auto* w = cfg.find("store_token_weights"); w && w->is_bool())
            m.store_token_weights = w->as_bool();
        SPP_RETURN_IF_ERROR(s.AddField(std::move(m)));
    }
    return s;
}

}  // namespace spp::index
