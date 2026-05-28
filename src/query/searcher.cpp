#include "spp/query/searcher.h"

#include "spp/analyze/analyzer.h"
#include "spp/index/schema.h"
#include "spp/index/segment_reader.h"
#include "spp/json/json_parser.h"
#include "spp/json/json_value.h"
#include "spp/query/features.h"
#include "spp/query/iterators.h"
#include "spp/query/query_parser.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spp::query {

namespace {

using spp::index::FieldRead;
using spp::index::FieldType;
using spp::index::IndexReader;
using spp::index::Schema;
using spp::index::SegmentReader;
using spp::index::TermEntry;

struct LeafTerm {
    std::string field_name;
    std::string text;
    double idf = 0.0;  // computed from global df across segments
};

// Walks the AST and collects (field, analyzed-term) leaves. Implicitly converts each
// QueryAst::Term into a conjunction of its analyzed sub-terms.
void CollectLeaves(const QueryAst& q, const Schema& schema, std::vector<LeafTerm>& out) {
    switch (q.kind()) {
        case QueryKind::kMatchAll:
            return;
        case QueryKind::kTerm: {
            const std::string& fname = q.field();
            const auto fid = schema.GetFieldId(fname);
            std::unique_ptr<spp::analyze::Analyzer> az;
            if (fid != kInvalidFieldId && schema.GetField(fid).type == FieldType::kKeyword) {
                az = spp::analyze::MakeKeywordAnalyzer();
            } else {
                az = spp::analyze::MakeStandardAnalyzer();
            }
            std::vector<spp::analyze::Token> toks;
            az->Analyze(q.text(), toks);
            for (auto& t : toks) {
                out.push_back(LeafTerm{fname, std::move(t.text), 0.0});
            }
            return;
        }
        case QueryKind::kConjunction:
        case QueryKind::kDisjunction:
            for (const auto& c : q.children())
                CollectLeaves(c, schema, out);
            return;
    }
}

// Build an iterator for a single QueryAst restricted to one segment.
std::unique_ptr<DocIdSetIterator> Build(const QueryAst& q,
                                        const SegmentReader& seg,
                                        const Schema& schema) {
    switch (q.kind()) {
        case QueryKind::kMatchAll:
            return std::make_unique<MatchAllIterator>(seg.doc_count());

        case QueryKind::kTerm: {
            const auto fid = seg.GetFieldId(q.field());
            if (fid == kInvalidFieldId)
                return std::make_unique<MatchAllIterator>(0);

            std::unique_ptr<spp::analyze::Analyzer> az;
            const auto schema_fid = schema.GetFieldId(q.field());
            if (schema_fid != kInvalidFieldId &&
                schema.GetField(schema_fid).type == FieldType::kKeyword) {
                az = spp::analyze::MakeKeywordAnalyzer();
            } else {
                az = spp::analyze::MakeStandardAnalyzer();
            }
            std::vector<spp::analyze::Token> toks;
            az->Analyze(q.text(), toks);
            if (toks.empty())
                return std::make_unique<MatchAllIterator>(0);

            std::vector<std::unique_ptr<DocIdSetIterator>> kids;
            kids.reserve(toks.size());
            const FieldRead* fr = seg.field(fid);
            const bool has_pos = fr != nullptr && fr->stats.has_positions;
            const bool has_w = fr != nullptr && fr->stats.has_token_weights;
            for (const auto& t : toks) {
                const TermEntry* te = seg.FindTerm(fid, t.text);
                if (te == nullptr) {
                    // A missing token makes the conjunction empty.
                    return std::make_unique<MatchAllIterator>(0);
                }
                kids.push_back(std::make_unique<TermIterator>(
                    seg.PostingBytes(fid, *te), te->df, te->total_tf, has_pos, has_w));
            }
            if (kids.size() == 1)
                return std::move(kids[0]);
            return std::make_unique<ConjunctionIterator>(std::move(kids));
        }

        case QueryKind::kConjunction: {
            std::vector<std::unique_ptr<DocIdSetIterator>> kids;
            kids.reserve(q.children().size());
            for (const auto& c : q.children())
                kids.push_back(Build(c, seg, schema));
            if (kids.empty())
                return std::make_unique<MatchAllIterator>(0);
            if (kids.size() == 1)
                return std::move(kids[0]);
            return std::make_unique<ConjunctionIterator>(std::move(kids));
        }

        case QueryKind::kDisjunction: {
            std::vector<std::unique_ptr<DocIdSetIterator>> kids;
            kids.reserve(q.children().size());
            for (const auto& c : q.children())
                kids.push_back(Build(c, seg, schema));
            if (kids.empty())
                return std::make_unique<MatchAllIterator>(0);
            if (kids.size() == 1)
                return std::move(kids[0]);
            return std::make_unique<DisjunctionIterator>(std::move(kids));
        }
    }
    return std::make_unique<MatchAllIterator>(0);
}

struct ScoredEntry {
    double score;
    std::uint32_t segment_index;
    DocId doc_id;
};

struct ScoredEntryCmp {
    // min-heap by score (worst is on top)
    bool operator()(const ScoredEntry& a, const ScoredEntry& b) const {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.segment_index != b.segment_index)
            return a.segment_index > b.segment_index;
        return a.doc_id > b.doc_id;
    }
};

}  // namespace

Expected<SearchResult> Searcher::Search(const QueryAst& q, const SearchOptions& opts) {
    const IndexReader& r = *reader_;
    SearchResult result;
    if (r.segments().empty())
        return result;

    // Compute global stats per leaf for BM25 idf.
    std::vector<LeafTerm> leaves;
    CollectLeaves(q, r.schema(), leaves);
    const std::uint64_t total_docs = r.total_doc_count();
    std::unordered_map<std::string, double> idf_by_key;
    auto key_of = [](const std::string& f, const std::string& t) { return f + ":" + t; };
    for (const auto& l : leaves) {
        const auto key = key_of(l.field_name, l.text);
        if (idf_by_key.count(key))
            continue;
        std::uint64_t global_df = 0;
        for (const auto& seg : r.segments()) {
            const auto fid = seg->GetFieldId(l.field_name);
            if (fid == kInvalidFieldId)
                continue;
            const TermEntry* te = seg->FindTerm(fid, l.text);
            if (te != nullptr)
                global_df += te->df;
        }
        idf_by_key[key] = Bm25Idf(total_docs, global_df);
    }

    // Per-field average length (across the whole index).
    std::unordered_map<std::string, double> avg_dl_by_field;
    std::unordered_map<std::string, std::uint64_t> sum_dl_by_field;
    std::unordered_map<std::string, std::uint64_t> doc_count_by_field;
    for (const auto& seg : r.segments()) {
        for (FieldId fid = 0; fid < seg->info().fields.size(); ++fid) {
            const auto& fs = seg->info().fields[fid];
            sum_dl_by_field[fs.name] += fs.sum_field_length;
            doc_count_by_field[fs.name] += fs.doc_count;
        }
    }
    for (const auto& [name, sum] : sum_dl_by_field) {
        const std::uint64_t n = doc_count_by_field[name];
        avg_dl_by_field[name] = (n == 0) ? 0.0 : static_cast<double>(sum) / static_cast<double>(n);
    }

    // Stage 1: BM25 candidate selection. We keep the top `rerank_top_n` docs
    // (by BM25 only) per query — that's the recall pool that Stage 2 reranks.
    // When no ranker is configured and the caller didn't ask for features we
    // collapse top_n down to `size` for v0.1 behavior (no extra work).
    const bool run_stage2 = opts.ranker != nullptr || opts.collect_features;
    const std::size_t top_n =
        run_stage2 ? std::max(opts.rerank_top_n, opts.size) : opts.size;

    std::priority_queue<ScoredEntry, std::vector<ScoredEntry>, ScoredEntryCmp> heap;
    std::uint64_t total_hits = 0;

    for (std::size_t si = 0; si < r.segments().size(); ++si) {
        const auto& seg = r.segments()[si];
        auto it = Build(q, *seg, r.schema());
        DocId d = it->Next();
        while (d != kNoMoreDocs) {
            ++total_hits;
            double score = 0.0;
            for (const auto& l : leaves) {
                const auto fid = seg->GetFieldId(l.field_name);
                if (fid == kInvalidFieldId)
                    continue;
                const TermEntry* te = seg->FindTerm(fid, l.text);
                if (te == nullptr)
                    continue;
                TermIterator term_it(seg->PostingBytes(fid, *te), te->df, te->total_tf);
                if (term_it.Advance(d) != d)
                    continue;
                const std::uint32_t tf = term_it.Freq();
                const double idf = idf_by_key[key_of(l.field_name, l.text)];
                // doc_len: we don't store per-doc field lengths in v0.2 — use the field average
                // as a proxy. Documented in features.h as a known approximation.
                const double avg = avg_dl_by_field[l.field_name];
                score += Bm25Score(idf, tf, avg, avg, opts.bm25);
            }
            if (heap.size() < top_n) {
                heap.push(ScoredEntry{score, static_cast<std::uint32_t>(si), d});
            } else if (score > heap.top().score) {
                heap.pop();
                heap.push(ScoredEntry{score, static_cast<std::uint32_t>(si), d});
            }
            d = it->Next();
        }
    }

    result.total_hits = total_hits;

    std::vector<ScoredEntry> candidates;
    candidates.reserve(heap.size());
    while (!heap.empty()) {
        candidates.push_back(heap.top());
        heap.pop();
    }

    // Stage 2: optional re-rank and/or feature collection. We rebuild per-leaf
    // state at each candidate doc and either feed the Ranker, capture the
    // FeatureVector, or both. Cost is O(top_n * num_leaves).
    std::vector<FeatureVector> features_by_index;
    if (run_stage2 && !candidates.empty()) {
        features_by_index.resize(candidates.size());
        const Schema& schema = r.schema();
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto& c = candidates[i];
            const auto& seg = r.segments()[c.segment_index];
            CandidateContext cctx;
            cctx.segment = seg.get();
            cctx.schema = &schema;
            cctx.doc_id = c.doc_id;
            cctx.num_query_leaves = leaves.size();
            cctx.leaves.reserve(leaves.size());
            for (const auto& l : leaves) {
                LeafContextItem item;
                item.idf = idf_by_key[key_of(l.field_name, l.text)];
                item.avg_field_len = avg_dl_by_field[l.field_name];
                item.field_len = item.avg_field_len;  // proxy, same as Stage 1

                const auto seg_fid = seg->GetFieldId(l.field_name);
                if (seg_fid != kInvalidFieldId) {
                    item.field_id = seg_fid;
                    if (const auto* fr = seg->field(seg_fid); fr != nullptr) {
                        item.boost = fr->stats.boost;
                        item.position_decay = fr->stats.position_decay;
                    }
                    const TermEntry* te = seg->FindTerm(seg_fid, l.text);
                    if (te != nullptr) {
                        const FieldRead* fr = seg->field(seg_fid);
                        const bool has_pos = fr != nullptr && fr->stats.has_positions;
                        const bool has_w = fr != nullptr && fr->stats.has_token_weights;
                        TermIterator term_it(seg->PostingBytes(seg_fid, *te), te->df,
                                             te->total_tf, has_pos, has_w);
                        if (term_it.Advance(c.doc_id) == c.doc_id) {
                            item.matched = true;
                            item.tf = term_it.Freq();
                            if (has_pos)
                                item.first_pos = term_it.Position();
                            if (has_w)
                                item.token_weight = term_it.TokenWeight();
                        }
                    }
                }
                cctx.leaves.push_back(item);
            }
            FeatureVector fv = ExtractFeatures(cctx, opts.bm25);
            if (opts.ranker != nullptr)
                c.score = static_cast<double>(opts.ranker->Score(fv));
            features_by_index[i] = fv;
        }
    }

    // Stage 3: sort, truncate to `size`, hydrate stored fields.
    // We sort indices first so we can re-attach the matching FeatureVector
    // alongside each Hit when collect_features is true.
    std::vector<std::size_t> order(candidates.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return candidates[a].score > candidates[b].score;
    });
    if (order.size() > opts.size)
        order.resize(opts.size);

    result.hits.reserve(order.size());
    if (opts.collect_features)
        result.features.reserve(order.size());
    for (std::size_t idx : order) {
        const auto& s = candidates[idx];
        Hit h;
        h.score = s.score;
        const auto& seg = r.segments()[s.segment_index];
        const std::string_view stored = seg->StoredFields(s.doc_id);
        h.stored_fields_json.assign(stored.data(), stored.size());
        auto parsed = spp::json::Parse(stored);
        if (parsed.ok() && parsed->is_object()) {
            if (const auto* idv = parsed->find(std::string{spp::index::Schema::kIdField});
                idv != nullptr && idv->is_string()) {
                h.id = idv->as_string();
            }
        }
        result.hits.push_back(std::move(h));
        if (opts.collect_features) {
            result.features.push_back(features_by_index[idx]);
        }
    }
    return result;
}

Expected<SearchResult> Searcher::Search(std::string_view query_str, const SearchOptions& opts) {
    auto ast = Parse(query_str, opts.default_field);
    if (!ast.ok())
        return ast.status();
    return Search(*ast, opts);
}

}  // namespace spp::query
