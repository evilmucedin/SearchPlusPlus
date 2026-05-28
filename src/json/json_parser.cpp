#include "spp/json/json_parser.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace spp::json {

namespace {

class Parser {
 public:
    explicit Parser(std::string_view input) : in_(input) {}

    Expected<JsonValue> Run() {
        SkipWs();
        auto v = ParseValue();
        if (!v.ok())
            return v.status();
        SkipWs();
        if (pos_ != in_.size())
            return Err("trailing data after JSON value");
        return v;
    }

 private:
    Status Err(std::string_view what) const {
        std::string msg;
        msg.append(what);
        msg.append(" at offset ");
        msg.append(std::to_string(pos_));
        return Status::InvalidArgument(std::move(msg));
    }

    bool Eof() const noexcept {
        return pos_ >= in_.size();
    }
    char Peek() const noexcept {
        return in_[pos_];
    }

    void SkipWs() noexcept {
        while (pos_ < in_.size()) {
            const char c = in_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    bool Consume(char c) noexcept {
        if (pos_ < in_.size() && in_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ConsumeLiteral(std::string_view lit) noexcept {
        if (in_.size() - pos_ < lit.size())
            return false;
        if (in_.compare(pos_, lit.size(), lit) != 0)
            return false;
        pos_ += lit.size();
        return true;
    }

    Expected<JsonValue> ParseValue() {
        SkipWs();
        if (Eof())
            return Err("unexpected end of input");
        const char c = Peek();
        if (c == '"')
            return ParseString();
        if (c == '{')
            return ParseObject();
        if (c == '[')
            return ParseArray();
        if (c == 't' || c == 'f')
            return ParseBool();
        if (c == 'n')
            return ParseNull();
        if (c == '-' || (c >= '0' && c <= '9'))
            return ParseNumber();
        return Err("unexpected character");
    }

    Expected<JsonValue> ParseNull() {
        if (!ConsumeLiteral("null"))
            return Err("expected 'null'");
        return JsonValue{nullptr};
    }

    Expected<JsonValue> ParseBool() {
        if (ConsumeLiteral("true"))
            return JsonValue{true};
        if (ConsumeLiteral("false"))
            return JsonValue{false};
        return Err("expected 'true' or 'false'");
    }

    Expected<JsonValue> ParseNumber() {
        const std::size_t start = pos_;
        if (Peek() == '-')
            ++pos_;
        // integer part
        if (Eof())
            return Err("unterminated number");
        if (Peek() == '0') {
            ++pos_;
        } else if (Peek() >= '1' && Peek() <= '9') {
            while (!Eof() && Peek() >= '0' && Peek() <= '9')
                ++pos_;
        } else {
            return Err("invalid number");
        }
        bool is_float = false;
        // fraction
        if (!Eof() && Peek() == '.') {
            is_float = true;
            ++pos_;
            if (Eof() || Peek() < '0' || Peek() > '9')
                return Err("expected digit after '.'");
            while (!Eof() && Peek() >= '0' && Peek() <= '9')
                ++pos_;
        }
        // exponent
        if (!Eof() && (Peek() == 'e' || Peek() == 'E')) {
            is_float = true;
            ++pos_;
            if (!Eof() && (Peek() == '+' || Peek() == '-'))
                ++pos_;
            if (Eof() || Peek() < '0' || Peek() > '9')
                return Err("expected digit in exponent");
            while (!Eof() && Peek() >= '0' && Peek() <= '9')
                ++pos_;
        }
        const std::string_view tok = in_.substr(start, pos_ - start);
        if (is_float) {
            // std::from_chars for double is C++17 but spotty on macOS toolchains; fall back to
            // strtod.
            std::string buf(tok);
            char* end = nullptr;
            const double d = std::strtod(buf.c_str(), &end);
            if (end != buf.c_str() + buf.size())
                return Err("invalid floating-point number");
            return JsonValue{d};
        }
        std::int64_t i = 0;
        const auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), i);
        if (ec != std::errc{} || ptr != tok.data() + tok.size()) {
            // Out-of-range or otherwise unparseable as int64: try as double instead.
            std::string buf(tok);
            char* end = nullptr;
            const double d = std::strtod(buf.c_str(), &end);
            if (end != buf.c_str() + buf.size())
                return Err("invalid number");
            return JsonValue{d};
        }
        return JsonValue{i};
    }

    Expected<JsonValue> ParseString() {
        std::string out;
        Status st = ParseStringInto(out);
        if (!st.ok())
            return st;
        return JsonValue{std::move(out)};
    }

    Status ParseStringInto(std::string& out) {
        if (!Consume('"'))
            return Err("expected '\"'");
        while (!Eof()) {
            const unsigned char uc = static_cast<unsigned char>(in_[pos_]);
            if (uc < 0x20)
                return Err("unescaped control character in string");
            if (uc == '"') {
                ++pos_;
                return Status::Ok();
            }
            if (uc == '\\') {
                ++pos_;
                if (Eof())
                    return Err("unterminated escape sequence");
                const char esc = in_[pos_++];
                switch (esc) {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u': {
                        if (in_.size() - pos_ < 4)
                            return Err("truncated \\u escape");
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            const char hc = in_[pos_ + static_cast<std::size_t>(k)];
                            unsigned d = 0;
                            if (hc >= '0' && hc <= '9')
                                d = static_cast<unsigned>(hc - '0');
                            else if (hc >= 'a' && hc <= 'f')
                                d = static_cast<unsigned>(hc - 'a' + 10);
                            else if (hc >= 'A' && hc <= 'F')
                                d = static_cast<unsigned>(hc - 'A' + 10);
                            else
                                return Err("invalid hex digit in \\u escape");
                            cp = (cp << 4u) | d;
                        }
                        pos_ += 4;
                        // Encode as UTF-8 (v0.1 ignores surrogate pairs; flagged in DESIGN if
                        // needed).
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        return Err("invalid escape character");
                }
                continue;
            }
            out.push_back(static_cast<char>(uc));
            ++pos_;
        }
        return Err("unterminated string");
    }

    Expected<JsonValue> ParseArray() {
        if (!Consume('['))
            return Err("expected '['");
        JsonArray arr;
        SkipWs();
        if (Consume(']'))
            return JsonValue{std::move(arr)};
        while (true) {
            auto elem = ParseValue();
            if (!elem.ok())
                return elem.status();
            arr.push_back(std::move(*elem));
            SkipWs();
            if (Consume(',')) {
                SkipWs();
                continue;
            }
            if (Consume(']'))
                return JsonValue{std::move(arr)};
            return Err("expected ',' or ']'");
        }
    }

    Expected<JsonValue> ParseObject() {
        if (!Consume('{'))
            return Err("expected '{'");
        JsonObject obj;
        SkipWs();
        if (Consume('}'))
            return JsonValue{std::move(obj)};
        while (true) {
            SkipWs();
            std::string key;
            Status st = ParseStringInto(key);
            if (!st.ok())
                return st;
            SkipWs();
            if (!Consume(':'))
                return Err("expected ':' after object key");
            SkipWs();
            auto val = ParseValue();
            if (!val.ok())
                return val.status();
            obj.insert_or_assign(std::move(key), std::move(*val));
            SkipWs();
            if (Consume(','))
                continue;
            if (Consume('}'))
                return JsonValue{std::move(obj)};
            return Err("expected ',' or '}'");
        }
    }

    std::string_view in_;
    std::size_t pos_ = 0;
};

}  // namespace

Expected<JsonValue> Parse(std::string_view input) {
    Parser p(input);
    return p.Run();
}

}  // namespace spp::json
