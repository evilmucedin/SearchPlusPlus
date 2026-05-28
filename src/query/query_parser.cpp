#include "spp/query/query_parser.h"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spp::query {

namespace {

bool IsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.';
}

class Parser {
 public:
    Parser(std::string_view in, std::string_view default_field)
        : in_(in), default_field_(default_field) {}

    Expected<QueryAst> Run() {
        SkipWs();
        if (Eof())
            return QueryAst::MatchAll();
        auto q = ParseOr();
        if (!q.ok())
            return q.status();
        SkipWs();
        if (!Eof())
            return Err("trailing characters");
        return q;
    }

 private:
    Status Err(std::string_view what) const {
        return Status::InvalidArgument(std::string{"query parse error at offset "} +
                                       std::to_string(pos_) + ": " + std::string{what});
    }

    bool Eof() const noexcept {
        return pos_ >= in_.size();
    }
    char Peek() const noexcept {
        return in_[pos_];
    }
    void SkipWs() noexcept {
        while (pos_ < in_.size() &&
               (in_[pos_] == ' ' || in_[pos_] == '\t' || in_[pos_] == '\n' || in_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool ConsumeKeyword(std::string_view kw) {
        SkipWs();
        if (in_.size() - pos_ < kw.size())
            return false;
        if (in_.compare(pos_, kw.size(), kw) != 0)
            return false;
        // Must be followed by a delimiter (whitespace, ')' or EOF).
        const std::size_t after = pos_ + kw.size();
        if (after < in_.size() && IsIdentChar(in_[after]))
            return false;
        pos_ = after;
        return true;
    }

    Expected<QueryAst> ParseOr() {
        auto left = ParseAnd();
        if (!left.ok())
            return left.status();
        std::vector<QueryAst> children;
        children.push_back(std::move(*left));
        while (true) {
            const std::size_t save = pos_;
            if (!ConsumeKeyword("OR")) {
                pos_ = save;
                break;
            }
            auto right = ParseAnd();
            if (!right.ok())
                return right.status();
            children.push_back(std::move(*right));
        }
        if (children.size() == 1)
            return std::move(children[0]);
        return QueryAst::Disjunction(std::move(children));
    }

    Expected<QueryAst> ParseAnd() {
        auto first = ParseAtom();
        if (!first.ok())
            return first.status();
        std::vector<QueryAst> children;
        children.push_back(std::move(*first));
        while (true) {
            const std::size_t save = pos_;
            (void)ConsumeKeyword("AND");  // optional keyword; otherwise implicit AND on next token
            SkipWs();
            if (Eof() || Peek() == ')' || LookingAt("OR")) {
                pos_ = save;
                break;
            }
            auto next = ParseAtom();
            if (!next.ok())
                return next.status();
            children.push_back(std::move(*next));
        }
        if (children.size() == 1)
            return std::move(children[0]);
        return QueryAst::Conjunction(std::move(children));
    }

    bool LookingAt(std::string_view kw) {
        const std::size_t save = pos_;
        SkipWs();
        const bool ok = ConsumeKeyword(kw);
        pos_ = save;
        return ok;
    }

    Expected<QueryAst> ParseAtom() {
        SkipWs();
        if (Eof())
            return Err("expected term");
        if (Peek() == '(') {
            ++pos_;
            auto inner = ParseOr();
            if (!inner.ok())
                return inner.status();
            SkipWs();
            if (Eof() || Peek() != ')')
                return Err("expected ')'");
            ++pos_;
            return inner;
        }
        // identifier (may be field qualifier) or quoted string
        std::string ident_or_term = ReadIdentOrQuoted();
        if (ident_or_term.empty())
            return Err("expected term");
        SkipWs();
        if (!Eof() && Peek() == ':') {
            // ident was a field; now read a term value.
            ++pos_;
            SkipWs();
            std::string term = ReadIdentOrQuoted();
            if (term.empty())
                return Err("expected term after ':'");
            return QueryAst::Term(std::move(ident_or_term), std::move(term));
        }
        return QueryAst::Term(std::string{default_field_}, std::move(ident_or_term));
    }

    std::string ReadIdentOrQuoted() {
        SkipWs();
        if (Eof())
            return {};
        if (Peek() == '"') {
            std::string out;
            ++pos_;
            while (!Eof() && Peek() != '"') {
                out.push_back(in_[pos_++]);
            }
            if (!Eof())
                ++pos_;  // closing quote
            return out;
        }
        std::string out;
        while (!Eof() && IsIdentChar(Peek())) {
            out.push_back(in_[pos_++]);
        }
        return out;
    }

    std::string_view in_;
    std::string_view default_field_;
    std::size_t pos_ = 0;
};

}  // namespace

Expected<QueryAst> Parse(std::string_view input, std::string_view default_field) {
    Parser p(input, default_field);
    return p.Run();
}

}  // namespace spp::query
