#include "spp/json/json_serializer.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace spp::json {

namespace {

void AppendIndent(std::string& out, int depth, int indent) {
    if (indent <= 0)
        return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(depth) * static_cast<std::size_t>(indent), ' ');
}

void WriteEscapedString(std::string_view s, std::string& out) {
    out.push_back('"');
    EscapeStringInto(s, out);
    out.push_back('"');
}

void WriteNumber(const JsonValue& v, std::string& out) {
    if (v.is_int()) {
        out.append(std::to_string(v.as_int()));
        return;
    }
    const double d = v.as_double();
    if (!std::isfinite(d)) {
        // JSON does not have inf/nan; emit null as the conservative thing.
        out.append("null");
        return;
    }
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "%.17g", d);
    if (n > 0)
        out.append(buf, static_cast<std::size_t>(n));
}

void Write(const JsonValue& v, std::string& out, int depth, int indent) {
    switch (v.type()) {
        case JsonType::kNull:
            out.append("null");
            return;
        case JsonType::kBool:
            out.append(v.as_bool() ? "true" : "false");
            return;
        case JsonType::kInt:
            [[fallthrough]];
        case JsonType::kDouble:
            WriteNumber(v, out);
            return;
        case JsonType::kString:
            WriteEscapedString(v.as_string(), out);
            return;
        case JsonType::kArray: {
            const auto& a = v.as_array();
            if (a.empty()) {
                out.append("[]");
                return;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i > 0)
                    out.push_back(',');
                AppendIndent(out, depth + 1, indent);
                Write(a[i], out, depth + 1, indent);
            }
            AppendIndent(out, depth, indent);
            out.push_back(']');
            return;
        }
        case JsonType::kObject: {
            const auto& o = v.as_object();
            if (o.empty()) {
                out.append("{}");
                return;
            }
            out.push_back('{');
            bool first = true;
            for (const auto& [k, val] : o) {
                if (!first)
                    out.push_back(',');
                first = false;
                AppendIndent(out, depth + 1, indent);
                WriteEscapedString(k, out);
                out.push_back(':');
                if (indent > 0)
                    out.push_back(' ');
                Write(val, out, depth + 1, indent);
            }
            AppendIndent(out, depth, indent);
            out.push_back('}');
            return;
        }
    }
}

}  // namespace

void EscapeStringInto(std::string_view in, std::string& out) {
    for (char c : in) {
        const unsigned char uc = static_cast<unsigned char>(c);
        switch (uc) {
            case '"':
                out.append("\\\"");
                break;
            case '\\':
                out.append("\\\\");
                break;
            case '\b':
                out.append("\\b");
                break;
            case '\f':
                out.append("\\f");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            default:
                if (uc < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", uc);
                    out.append(buf);
                } else {
                    out.push_back(static_cast<char>(uc));
                }
        }
    }
}

std::string Serialize(const JsonValue& v) {
    std::string out;
    Write(v, out, 0, 0);
    return out;
}

std::string SerializePretty(const JsonValue& v, int indent) {
    std::string out;
    Write(v, out, 0, indent);
    return out;
}

}  // namespace spp::json
