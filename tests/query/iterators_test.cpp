#include "spp/query/iterators.h"

#include "spp/base/types.h"
#include "spp/store/varbyte.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// Build the posting bytes layout that TermIterator expects:
//   [df varuint][for each doc: delta varuint, tf varuint]
std::string MakePostingBytes(const std::vector<std::pair<spp::DocId, std::uint32_t>>& docs) {
    std::string out;
    spp::store::EncodeVarUint32(static_cast<std::uint32_t>(docs.size()), out);
    spp::DocId prev = 0;
    for (auto [id, tf] : docs) {
        spp::store::EncodeVarUint32(id - prev, out);
        spp::store::EncodeVarUint32(tf, out);
        prev = id;
    }
    return out;
}

std::vector<spp::DocId> DrainNext(spp::query::DocIdSetIterator& it) {
    std::vector<spp::DocId> ids;
    while (true) {
        const auto d = it.Next();
        if (d == spp::kNoMoreDocs)
            break;
        ids.push_back(d);
    }
    return ids;
}

}  // namespace

TEST(TermIteratorTest, IteratesAllDocs) {
    auto bytes = MakePostingBytes({{2, 1}, {7, 3}, {12, 2}});
    spp::query::TermIterator it(bytes, 3, 6);
    EXPECT_EQ(DrainNext(it), (std::vector<spp::DocId>{2, 7, 12}));
}

TEST(TermIteratorTest, AdvanceSkipsAhead) {
    auto bytes = MakePostingBytes({{2, 1}, {7, 1}, {12, 1}, {18, 1}});
    spp::query::TermIterator it(bytes, 4, 4);
    EXPECT_EQ(it.Advance(10), 12u);
    EXPECT_EQ(it.Next(), 18u);
    EXPECT_EQ(it.Next(), spp::kNoMoreDocs);
}

TEST(ConjunctionIteratorTest, IntersectsTwoLists) {
    auto a = MakePostingBytes({{1, 1}, {3, 1}, {5, 1}, {7, 1}});
    auto b = MakePostingBytes({{3, 2}, {4, 1}, {7, 1}, {9, 1}});
    std::vector<std::unique_ptr<spp::query::DocIdSetIterator>> kids;
    kids.push_back(std::make_unique<spp::query::TermIterator>(a, 4, 4));
    kids.push_back(std::make_unique<spp::query::TermIterator>(b, 4, 5));
    spp::query::ConjunctionIterator conj(std::move(kids));
    EXPECT_EQ(DrainNext(conj), (std::vector<spp::DocId>{3, 7}));
}

TEST(DisjunctionIteratorTest, UnionsTwoLists) {
    auto a = MakePostingBytes({{1, 1}, {5, 1}});
    auto b = MakePostingBytes({{2, 1}, {5, 1}, {9, 1}});
    std::vector<std::unique_ptr<spp::query::DocIdSetIterator>> kids;
    kids.push_back(std::make_unique<spp::query::TermIterator>(a, 2, 2));
    kids.push_back(std::make_unique<spp::query::TermIterator>(b, 3, 3));
    spp::query::DisjunctionIterator dis(std::move(kids));
    EXPECT_EQ(DrainNext(dis), (std::vector<spp::DocId>{1, 2, 5, 9}));
}

TEST(MatchAllIteratorTest, Enumerates) {
    spp::query::MatchAllIterator it(3);
    EXPECT_EQ(DrainNext(it), (std::vector<spp::DocId>{0, 1, 2}));
}
