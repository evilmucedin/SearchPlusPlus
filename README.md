# SearchPlusPlus

An ElasticSearch-inspired full-text search engine written from scratch in C++20.
Built as a library you can embed, with an HTTP server, CLI tools, and Python
bindings layered on top of the same core.

[![CI](https://github.com/evilmucedin/SearchPlusPlus/actions/workflows/ci.yml/badge.svg)](https://github.com/evilmucedin/SearchPlusPlus/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

## What it does

- **Indexes documents** into immutable on-disk segments with a hand-rolled file
  format tuned for `mmap`-friendly reads.
- **Searches them** with a BM25 scorer, a small boolean+phrase query language
  (`title:cat AND body:dog`), and a two-stage candidate-generation + re-ranking
  pipeline.
- **Re-ranks with learning-to-rank** — top-N BM25 candidates feed a
  feature extractor (BM25 per field, position decay, token weights, doc
  quality, …) into a pluggable `Ranker`: `Bm25Ranker`, `LinearRanker`, or a
  `CatboostRanker` that wraps a `save_model(format='cpp')` export.
- **Serves over HTTP** in NRT mode — `POST /:index/_doc`, `GET
  /:index/_search?q=...` — backed by a single-writer / many-reader concurrency
  model with a translog for crash recovery.

## Quick taste

C++:

```cpp
#include "spp/spp.h"

spp::index::Schema schema;
schema.AddTextFields({"title", "body"});

spp::index::IndexOpenOptions opts; opts.initial_schema = &schema;
auto writer = std::move(*spp::index::IndexWriter::Open("/tmp/idx", opts));
spp::index::Document d; d.id = "a";
d.fields = {{"title", "Inverted index"}, {"body", "Maps terms to docs."}};
writer->AddDocument(d);
writer->Refresh();

spp::query::Searcher s(writer->CurrentReader());
spp::query::SearchOptions q; q.default_field = "body";
auto r = *s.Search("inverted index", q);
for (const auto& h : r.hits) std::printf("%s  %.4f\n", h.id.c_str(), h.score);
```

Python (same engine, via the optional pybind11 bindings):

```python
import searchplusplus as spp

schema = spp.Schema(); schema.add_text_fields(["title", "body"])
w = spp.IndexWriter.open("/tmp/idx", initial_schema=schema)
d = spp.Document(); d.id = "a"
d.fields = {"title": "Inverted index", "body": "Maps terms to docs."}
w.add_document(d); w.refresh()

s = spp.Searcher(w.current_reader())
for hit in s.search("inverted index", default_field="body").hits:
    print(hit.id, hit.score)
```

Full end-to-end examples live in [`examples/`](examples/) — C++ in
[`hello_search/main.cpp`](examples/hello_search/main.cpp), Python in
[`python/hello_search.py`](examples/python/hello_search.py).

## Build

```bash
# Bootstraps system deps (apt/brew/dnf/pacman/zypper) and vcpkg.
./scripts/install.sh
./scripts/build.sh                    # configures + builds + runs ctest
```

Or directly with CMake:

```bash
cmake --preset default                # Debug + ASan/UBSan + tests + benches
cmake --build --preset default -j
ctest --preset default --output-on-failure
```

A Bazel build is also available for monorepo consumers — same sources, same
binaries, same test suite:

```bash
bazel build //...
bazel test //tests/...
```

CI matrix runs on Ubuntu (gcc, clang, asan+ubsan, bazel), macOS (Apple LLVM),
and Windows (MSVC). All green.

## What's in here

| Path | What lives there |
|---|---|
| `include/spp/`, `src/` | The library (`spp_core`): `analyze`, `index`, `query`, `store`, `json`, `http`, `server`. |
| `tools/spp_serve/` | HTTP server. `spp_serve --index /path --port 9200`. |
| `tools/spp_index/`, `spp_search/` | Offline indexer + query CLI. |
| `tools/spp_export_features/` | Dump a CatBoost training pool from a judgments file. |
| `examples/` | Minimal end-to-end C++ and Python programs. |
| `python/` | Optional pybind11 bindings (`SPP_BUILD_PYTHON=ON`). |
| `bench/`, `tests/` | Google Benchmark + GoogleTest suites. |

## Where to read next

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — component layout, dataflow, file format.
- [`DESIGN.md`](DESIGN.md) — load-bearing design decisions and their rationale,
  including the deliberately small dependency budget.
- [`CLAUDE.md`](CLAUDE.md) — founding principles (performance is the headline
  goal; Linux primary, macOS + Windows supported; tests and benches are
  first-class).

## Status

Early-stage but functional: v0.1 shipped the indexer + HTTP server, v0.2 added
learning-to-rank with CatBoost, plus per-field boost, position decay, per-token
weights, and static doc quality. The API surface should be treated as
unstable until a 1.0 cut.

## License

Apache-2.0 — see [`LICENSE`](LICENSE).
