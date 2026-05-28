#include "spp/analyze/analyzer.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace spp::analyze {

namespace {

bool IsTokenChar(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

char AsciiLower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
}

const std::unordered_set<std::string_view>& StopWords() {
    static const std::unordered_set<std::string_view> kStopWords = {
        "a",
        "an",
        "the",
        "and",
        "or",
        "not",
        "of",
        "to",
        "in",
        "is",
        "it",
        "for",
        "on",
        "with",
        "as",
        "by",
        "at",
        "from",
    };
    return kStopWords;
}

class StandardAnalyzer final : public Analyzer {
 public:
    void Analyze(std::string_view input, std::vector<Token>& out) const override {
        out.clear();
        std::uint32_t position = 0;
        std::size_t i = 0;
        while (i < input.size()) {
            while (i < input.size() && !IsTokenChar(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            if (i >= input.size())
                break;
            const std::size_t start = i;
            while (i < input.size() && IsTokenChar(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            const std::size_t end = i;
            std::string lower;
            lower.reserve(end - start);
            for (std::size_t k = start; k < end; ++k) {
                lower.push_back(AsciiLower(static_cast<unsigned char>(input[k])));
            }
            if (StopWords().contains(std::string_view{lower})) {
                ++position;  // still advances positions so phrase positions stay meaningful later
                continue;
            }
            out.push_back(Token{
                std::move(lower),
                position,
                static_cast<std::uint32_t>(start),
                static_cast<std::uint32_t>(end),
            });
            ++position;
        }
    }

    std::string Fingerprint() const override {
        return "standard:v1";
    }
};

class KeywordAnalyzer final : public Analyzer {
 public:
    void Analyze(std::string_view input, std::vector<Token>& out) const override {
        out.clear();
        if (input.empty())
            return;
        out.push_back(Token{
            std::string{input},
            0,
            0,
            static_cast<std::uint32_t>(input.size()),
        });
    }

    std::string Fingerprint() const override {
        return "keyword:v1";
    }
};

}  // namespace

std::unique_ptr<Analyzer> MakeStandardAnalyzer() {
    return std::make_unique<StandardAnalyzer>();
}

std::unique_ptr<Analyzer> MakeKeywordAnalyzer() {
    return std::make_unique<KeywordAnalyzer>();
}

}  // namespace spp::analyze
