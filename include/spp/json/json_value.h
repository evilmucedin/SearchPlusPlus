#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spp::json {

class JsonValue;

using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;  // ordered for stable output

enum class JsonType : std::uint8_t {
    kNull,
    kBool,
    kInt,
    kDouble,
    kString,
    kArray,
    kObject,
};

class JsonValue {
 public:
    JsonValue() noexcept : type_(JsonType::kNull) {}
    JsonValue(std::nullptr_t) noexcept : type_(JsonType::kNull) {}
    JsonValue(bool b) noexcept : type_(JsonType::kBool), b_(b) {}
    JsonValue(int i) noexcept : type_(JsonType::kInt), i_(i) {}
    JsonValue(std::int64_t i) noexcept : type_(JsonType::kInt), i_(i) {}
    JsonValue(double d) noexcept : type_(JsonType::kDouble), d_(d) {}
    JsonValue(const char* s) : type_(JsonType::kString), s_(s) {}
    JsonValue(std::string s) : type_(JsonType::kString), s_(std::move(s)) {}
    JsonValue(std::string_view s) : type_(JsonType::kString), s_(s) {}
    JsonValue(JsonArray a)
        : type_(JsonType::kArray), array_(std::make_unique<JsonArray>(std::move(a))) {}
    JsonValue(JsonObject o)
        : type_(JsonType::kObject), object_(std::make_unique<JsonObject>(std::move(o))) {}
    JsonValue(std::initializer_list<std::pair<const std::string, JsonValue>> il)
        : type_(JsonType::kObject), object_(std::make_unique<JsonObject>(il.begin(), il.end())) {}

    JsonValue(const JsonValue& other) {
        CopyFrom(other);
    }
    JsonValue& operator=(const JsonValue& other) {
        if (this != &other) {
            Reset();
            CopyFrom(other);
        }
        return *this;
    }
    JsonValue(JsonValue&& other) noexcept {
        MoveFrom(std::move(other));
    }
    JsonValue& operator=(JsonValue&& other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }
    ~JsonValue() {
        Reset();
    }

    JsonType type() const noexcept {
        return type_;
    }
    bool is_null() const noexcept {
        return type_ == JsonType::kNull;
    }
    bool is_bool() const noexcept {
        return type_ == JsonType::kBool;
    }
    bool is_int() const noexcept {
        return type_ == JsonType::kInt;
    }
    bool is_double() const noexcept {
        return type_ == JsonType::kDouble;
    }
    bool is_number() const noexcept {
        return is_int() || is_double();
    }
    bool is_string() const noexcept {
        return type_ == JsonType::kString;
    }
    bool is_array() const noexcept {
        return type_ == JsonType::kArray;
    }
    bool is_object() const noexcept {
        return type_ == JsonType::kObject;
    }

    bool as_bool() const noexcept {
        return b_;
    }
    std::int64_t as_int() const noexcept {
        return i_;
    }
    double as_double() const noexcept {
        return is_int() ? static_cast<double>(i_) : d_;
    }
    const std::string& as_string() const noexcept {
        return s_;
    }
    const JsonArray& as_array() const {
        return *array_;
    }
    JsonArray& as_array() {
        return *array_;
    }
    const JsonObject& as_object() const {
        return *object_;
    }
    JsonObject& as_object() {
        return *object_;
    }

    // Object-style accessors. Caller must know the type.
    const JsonValue* find(std::string_view key) const {
        if (!is_object())
            return nullptr;
        auto it = object_->find(std::string{key});
        return it == object_->end() ? nullptr : &it->second;
    }

 private:
    void Reset() noexcept {
        s_.clear();
        s_.shrink_to_fit();
        array_.reset();
        object_.reset();
        type_ = JsonType::kNull;
        i_ = 0;
        d_ = 0.0;
        b_ = false;
    }
    void CopyFrom(const JsonValue& o) {
        type_ = o.type_;
        b_ = o.b_;
        i_ = o.i_;
        d_ = o.d_;
        s_ = o.s_;
        if (o.array_)
            array_ = std::make_unique<JsonArray>(*o.array_);
        if (o.object_)
            object_ = std::make_unique<JsonObject>(*o.object_);
    }
    void MoveFrom(JsonValue&& o) noexcept {
        type_ = o.type_;
        b_ = o.b_;
        i_ = o.i_;
        d_ = o.d_;
        s_ = std::move(o.s_);
        array_ = std::move(o.array_);
        object_ = std::move(o.object_);
        o.type_ = JsonType::kNull;
    }

    JsonType type_;
    bool b_ = false;
    std::int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::unique_ptr<JsonArray> array_;
    std::unique_ptr<JsonObject> object_;
};

}  // namespace spp::json
