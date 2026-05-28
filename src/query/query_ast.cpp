#include "spp/query/query_ast.h"

#include <string>
#include <utility>

namespace spp::query {

QueryAst QueryAst::Term(std::string field, std::string text) {
    QueryAst q;
    q.kind_ = QueryKind::kTerm;
    q.field_ = std::move(field);
    q.text_ = std::move(text);
    return q;
}

QueryAst QueryAst::Conjunction(std::vector<QueryAst> children) {
    QueryAst q;
    q.kind_ = QueryKind::kConjunction;
    q.children_ = std::move(children);
    return q;
}

QueryAst QueryAst::Disjunction(std::vector<QueryAst> children) {
    QueryAst q;
    q.kind_ = QueryKind::kDisjunction;
    q.children_ = std::move(children);
    return q;
}

QueryAst QueryAst::MatchAll() {
    QueryAst q;
    q.kind_ = QueryKind::kMatchAll;
    return q;
}

std::string QueryAst::ToString() const {
    switch (kind_) {
        case QueryKind::kMatchAll:
            return "*";
        case QueryKind::kTerm:
            return field_ + ":" + text_;
        case QueryKind::kConjunction:
        case QueryKind::kDisjunction: {
            std::string s = "(";
            const char* op = (kind_ == QueryKind::kConjunction) ? " AND " : " OR ";
            for (std::size_t i = 0; i < children_.size(); ++i) {
                if (i > 0)
                    s += op;
                s += children_[i].ToString();
            }
            s += ")";
            return s;
        }
    }
    return "<?>";
}

}  // namespace spp::query
