# Learning-to-rank with CatBoost

SearchPlusPlus v0.2 ships a learned-ranking layer that runs as a second stage on top of the BM25 candidate walk. BM25 picks the recall pool; a trained ranker re-scores it. This guide is the round-trip: index → judgments → pool → CatBoost → C++ export → redeploy.

The same shape applies to any GBDT trainer that can emit a function with signature `double f(const std::vector<float>&, const std::vector<std::string>&)`; CatBoost just happens to ship that exporter (`save_model(format='cpp')`) out of the box.

## Prerequisites

- A built SearchPlusPlus tree (`cmake --preset default && cmake --build --preset default -j`).
- Python with `catboost` installed (`pip install catboost`).
- A judgments file: a JSONL with one object per line, schema:
  ```json
  {"query": "search engine", "doc_id": "wiki:12345", "label": 3}
  ```
  `label` is an integer relevance grade (0 = irrelevant, 4 = perfect; CatBoost works with any small integer range). Optional `query_id`: integer used to group rows for ranking losses. When omitted, `spp_export_features` auto-assigns one per unique query string.

## 1. Build the index with LTR fields

Pick what you want to capture per field. The schema can mix and match:

```json
{
  "mappings": {
    "title": {"type": "text", "stored": true, "boost": 2.0},
    "body":  {"type": "text", "stored": true,
              "position_decay": 0.5,
              "store_token_weights": true}
  },
  "store_doc_quality": true
}
```

Cost:

| Option                        | Storage cost          | Feature(s) it enables       |
|-------------------------------|-----------------------|-----------------------------|
| `boost: N`                    | none (just `.si`)     | scales `bm25_field*`        |
| `position_decay: D`           | +2 bytes / posting    | `position_decay_sum`        |
| `store_token_weights: true`   | +1 byte / posting     | `token_weight_sum`, `_max`  |
| `store_doc_quality: true`     | +4 bytes / doc (.dvq) | `doc_quality`               |

Index your corpus. Per-token weights are passed as `<field>_weights: [floats]` on each doc; static quality is `_quality: <float>`. The server silently drops these fields when the schema didn't opt into the matching storage, so it's safe to send them speculatively.

## 2. Export a CatBoost pool

```bash
./build/default/tools/spp_export_features/spp_export_features \
  --index /var/lib/spp/wiki \
  --judgments judgments.jsonl \
  --out pool.tsv \
  --top-n 1000
```

`spp_export_features` runs each query in the judgments file against the index, finds the row whose `_id` matches `doc_id`, and writes one TSV row per judgment:

```
<label>\t<query_id>\t<f0>\t<f1>\t...\t<f15>
```

Judged docs that didn't make the top-N candidate pool are emitted with an all-zero feature row so the model learns to push them down. The TSV maps onto CatBoost's pool format with this column description (`pool.cd`):

```
0	Label
1	GroupId
```

(Columns 2..17 default to numeric features, which is what we want.)

## 3. Train

```bash
catboost fit \
  --loss-function YetiRank \
  --eval-metric NDCG \
  --learn-set pool.tsv \
  --cd pool.cd \
  -i 1000 \
  -m model.bin
```

`YetiRank` is the default pairwise ranking loss. For a small judgments set, start with fewer trees and shallower depth:

```bash
catboost fit \
  --loss-function YetiRank --eval-metric NDCG \
  --learn-set pool.tsv --cd pool.cd \
  --depth 6 --iterations 200 \
  -m model.bin
```

## 4. Export to C++

```bash
python - <<'PY'
from catboost import CatBoost
m = CatBoost()
m.load_model('model.bin')
m.save_model('catboost_model.cpp', format='cpp')
PY
```

`save_model(format='cpp')` writes a self-contained translation unit that defines `apply_catboost_model(const std::vector<float>&, const std::vector<std::string>&)`. The single-symbol contract is what `spp::query::CatboostRanker` calls into.

## 5. Re-deploy

```bash
cp catboost_model.cpp /path/to/SearchPlusPlus/models/catboost_model.cpp
cmake --preset default
cmake --build --preset default -j
./build/default/tools/spp_serve/spp_serve --index /var/lib/spp/wiki &
curl 'http://localhost:9200/wiki/_search?q=search+engine&rerank=true&ranker=catboost'
```

The default `models/catboost_model.cpp` checked into the repo is a stub that returns `features[0]` (i.e. BM25). It keeps `SPP_WITH_LTR_MODEL=ON` always buildable and CI runs the `catboost` ranker path against this trivial model. When you ship a real export, replace the file and rebuild — no other source changes are needed.

## Pointing at an out-of-tree model

If you'd rather keep your CatBoost export outside the SearchPlusPlus source tree:

```bash
cmake --preset default \
  -DSPP_WITH_LTR_MODEL=ON \
  -DSPP_LTR_MODEL_FILE=/etc/spp/catboost_model.cpp
```

`SPP_LTR_MODEL_FILE` is a cache var; pass it once on configure and rebuilds will pick it up.

## Hand-tuning with the linear ranker

For sanity-checking the two-stage pipeline before you commit to training a GBDT, push a weight vector at the running server:

```bash
curl -X PUT 'http://localhost:9200/wiki/_ltr/linear' \
  -H 'Content-Type: application/json' \
  -d '{"bias": 0.0, "weights": [1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5, 0.3, 0, 1.0]}'

curl 'http://localhost:9200/wiki/_search?q=search&rerank=true&ranker=linear'
```

The weights array must have exactly `kFeatureCount` entries (16 in v0.2). `GET /:index/_ltr` echoes back the active config including human-readable feature names — useful when wiring weights from a spreadsheet.

## Feature schema

The slot order and meaning is fixed by `include/spp/query/features.h`:

| Slot | Name                | Source                                     |
|------|---------------------|--------------------------------------------|
| 0    | `bm25_total`        | sum of BM25 over matched leaves            |
| 1    | `tf_sum`            | sum of `tf` over matched leaves            |
| 2    | `idf_sum`           | sum of leaf IDFs                           |
| 3    | `num_matched_terms` | how many query leaves matched this doc     |
| 4    | `num_query_terms`   | total leaves in the parsed query           |
| 5    | `match_ratio`       | `matched / total`                          |
| 6    | `doc_length_avg`    | reserved (0.0 in v0.2 — no per-doc lengths)|
| 7    | `doc_length_min`    | reserved (0.0 in v0.2)                     |
| 8–11 | `bm25_field[0..3]`  | BM25 per first 4 fields, by field id       |
| 12   | `position_decay_sum`| `sum(exp(-decay * pos / field_len))`       |
| 13   | `token_weight_sum`  | sum of stored token weights                |
| 14   | `token_weight_max`  | max stored token weight                    |
| 15   | `doc_quality`       | value from `.dvq` stripe (0.0 if absent)   |

Any time the schema changes — adding a slot, reordering, removing a slot — bump `kFeatureSchemaVersion`. The trained CatBoost model must match.
