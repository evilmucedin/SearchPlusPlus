#pragma once

#include <memory>
#include <string>
#include <vector>

namespace spp::query {

enum class QueryKind {
    kTerm,
    kConjunction,
    kDisjunction,
    kMatchAll,
};

class QueryAst {
 public:
    static QueryAst Term(std::string field, std::string text);
    static QueryAst Conjunction(std::vector<QueryAst> children);
    static QueryAst Disjunction(std::vector<QueryAst> children);
    static QueryAst MatchAll();

    QueryKind kind() const noexcept {
        return kind_;
    }

    const std::string& field() const noexcept {
        return field_;
    }
    const std::string& text() const noexcept {
        return text_;
    }
    const std::vector<QueryAst>& children() const noexcept {
        return children_;
    }
    std::vector<QueryAst>& children() noexcept {
        return children_;
    }

    // Pretty-print for debugging / tests.
    std::string ToString() const;

 private:
    QueryKind kind_ = QueryKind::kMatchAll;
    std::string field_;
    std::string text_;
    std::vector<QueryAst> children_;
};

}  // namespace spp::query
