// Verifies that the v0.2 LTR-extended storage stripes (positions, token
// weights, doc-quality) survive a write → seal → read roundtrip and surface
// through the segment-reader and term-iterator APIs.

#include "spp/index/document.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/index/segment_info.h"
#include "spp/index/segment_reader.h"
#include "spp/json/json_parser.h"
#include "spp/query/iterators.h"

#include <filesystem>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path TmpDir() {
    auto p = std::filesystem::temp_directory_path() /
             ("spp_ltr_storage_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(p);
    return p;
}

spp::index::Schema MakeLtrSchema() {
    // body opts into BOTH positions (via position_decay > 0) and token weights.
    auto v = spp::json::Parse(R"({
      "title": {"type":"text","stored":true,"boost":2.0},
      "body":  {"type":"text","stored":true,
                "position_decay":0.5,
                "store_token_weights":true}
    })")
                 .value();
    auto s = spp::index::Schema::FromMappingsJson(v).value();
    s.set_store_doc_quality(true);
    return s;
}

}  // namespace

TEST(LtrStorageTest, PositionsTokenWeightsAndQualityRoundtrip) {
    auto root = TmpDir();
    auto schema = MakeLtrSchema();
    spp::index::IndexOpenOptions opts;
    opts.initial_schema = &schema;
    auto writer = spp::index::IndexWriter::Open(root, opts).value();

    spp::index::Document d;
    d.id = "doc1";
    d.fields["title"] = "alpha beta gamma";
    d.fields["body"] = "alpha beta gamma";
    // Weights aligned with the body token stream.
    d.field_token_weights["body"] = {0.5f, 2.0f, 1.5f};
    d.doc_quality = 0.75f;
    ASSERT_TRUE(writer
                    ->AddDocument(
                        d, R"({"_id":"doc1","title":"alpha beta gamma","body":"alpha beta gamma"})")
                    .ok());

    ASSERT_TRUE(writer->Refresh().ok());
    auto reader = writer->CurrentReader();
    ASSERT_EQ(reader->segment_count(), 1u);
    const auto& seg = reader->segments()[0];

    // doc-quality stripe should be present and carry the value we sent.
    ASSERT_TRUE(seg->has_doc_quality());
    EXPECT_FLOAT_EQ(seg->DocQuality(0), 0.75f);

    // body field stats should record the v2 extras.
    const auto body_fid = seg->GetFieldId("body");
    ASSERT_NE(body_fid, spp::kInvalidFieldId);
    const auto* body_fr = seg->field(body_fid);
    ASSERT_NE(body_fr, nullptr);
    EXPECT_TRUE(body_fr->stats.has_positions);
    EXPECT_TRUE(body_fr->stats.has_token_weights);
    EXPECT_GT(body_fr->stats.position_decay, 0.0f);

    // title field has only a boost; no positions or weights.
    const auto title_fid = seg->GetFieldId("title");
    ASSERT_NE(title_fid, spp::kInvalidFieldId);
    const auto* title_fr = seg->field(title_fid);
    ASSERT_NE(title_fr, nullptr);
    EXPECT_FLOAT_EQ(title_fr->stats.boost, 2.0f);
    EXPECT_FALSE(title_fr->stats.has_positions);
    EXPECT_FALSE(title_fr->stats.has_token_weights);

    // Term iterator on body:"beta" should report position 1 (second token) and
    // the matching token weight (2.0, quantized).
    const auto* te_beta = seg->FindTerm(body_fid, "beta");
    ASSERT_NE(te_beta, nullptr);
    spp::query::TermIterator it(seg->PostingBytes(body_fid, *te_beta),
                                te_beta->df,
                                te_beta->total_tf,
                                /*has_positions=*/true,
                                /*has_token_weights=*/true);
    ASSERT_EQ(it.Next(), 0);  // doc0 is the only doc
    EXPECT_EQ(it.Position(), 1u);
    EXPECT_NEAR(it.TokenWeight(), 2.0f, 0.05f);  // q8 round-trip tolerance

    // First token of body ("alpha") should report position 0, weight 0.5.
    const auto* te_alpha = seg->FindTerm(body_fid, "alpha");
    ASSERT_NE(te_alpha, nullptr);
    spp::query::TermIterator it2(seg->PostingBytes(body_fid, *te_alpha),
                                 te_alpha->df,
                                 te_alpha->total_tf,
                                 /*has_positions=*/true,
                                 /*has_token_weights=*/true);
    ASSERT_EQ(it2.Next(), 0);
    EXPECT_EQ(it2.Position(), 0u);
    EXPECT_NEAR(it2.TokenWeight(), 0.5f, 0.05f);

    ASSERT_TRUE(writer->Close().ok());
    std::filesystem::remove_all(root);
}
