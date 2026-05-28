# Architecture

This document describes the **proposed** architecture of SearchPlusPlus. The repository is currently a green field; the structure below is a target, not a snapshot of existing code. When code lands, the goal is for the directory layout, module boundaries, and dataflow described here to match reality. If implementation diverges, update this document in the same change.

## Goals and non-goals

**Goals.** SearchPlusPlus is a library-first, embeddable C++ full-text search engine. The primary use cases are (1) being linked into a host process that wants high-performance local search over millions to low-billions of documents on a single node, and (2) being driven from a thin CLI or future RPC server without changing the core.

- Sub-millisecond mean latency on common boolean and phrase queries over an index that fits in RAM, with graceful degradation when the index spills to mmap'd files.
- A small, stable public API surface (`include/spp/`) — the contract with embedders.
- Predictable resource usage: no hidden allocations on the hot path, no hidden threads, no implicit global state.
- A clean separation between *what* the engine does (the query model) and *how* it stores bytes (the segment format), so the storage format can evolve without breaking callers.

**Non-goals (for now).** Distribution, replication, and sharding across machines. Online updates with strict freshness SLAs (the model is near-real-time at best, see *Concurrency* in `DESIGN.md`). Vector / ANN search as a primary mode — vector fields may be supported later as an auxiliary index type but the core is lexical. SQL or full relational semantics.

## The four layers

The engine is organized as four layers with a strict, one-directional dependency graph:

```
                ┌────────────┐
                │   query    │   parsing, planning, scoring, top-K
                └─────┬──────┘
                      │
        ┌─────────────┴────────────┐
        ▼                          ▼
   ┌─────────┐                ┌─────────┐
   │ analyze │                │  index  │   inverted index, segments, merges
   └────┬────┘                └────┬────┘
        │                          │
        └─────────────┬────────────┘
                      ▼
                ┌────────────┐
                │   store    │   bytes on disk, mmap, codecs, IO
                └────────────┘
```

- **`store`** owns the on-disk representation: segment files, posting-list codecs (VarByte / PForDelta / Roaring for low-density terms), the file-format version envelope, mmap lifetime, and the directory abstraction. It exposes typed *streams* and *readers* — never raw byte buffers — to the layers above. It depends on nothing inside `spp`.
- **`analyze`** turns text into tokens: Unicode normalization (NFKC), case folding, language-aware tokenization (ICU word-break), stop-word filtering, stemming. Analyzers are pure functions of `(text, config) → tokens`. The same analyzer must be used at index time and query time; this is enforced by pinning the analyzer config into the segment metadata. The default analyzer is **English-tuned** (English tokenizer, English stop words, Snorkel/Porter2 stemmer) — see principle 5 in `DESIGN.md`. Non-English analyzers (ICU-segmenter-based for CJK, language-specific stemmers for major European languages) are first-class and selected per field at index creation; the engine core never assumes English. When `SPP_WITH_ICU=OFF`, only the ASCII English analyzer is available.
- **`index`** owns the inverted index and the segment lifecycle. It composes `analyze` (to tokenize incoming documents) and `store` (to persist segments). It exposes a *writer* (single-threaded, accumulates an in-memory segment and flushes) and a *reader* (multi-threaded, opens sealed segments and serves term lookups).
- **`query`** is the user-facing query layer. It parses a query string into an AST, plans an execution tree of posting-list iterators against an index reader, and scores hits with BM25 by default. It never reaches into `store` directly; all byte-level concerns are mediated by `index`.

A cross-layer call that would invert this graph (e.g. `store` reaching into `query`) is a design smell and should be discussed in the PR rather than merged.

## Directory layout

```
include/spp/            public API headers, namespaced by module
  analyze/
  index/
  query/
  store/
  spp.h                 umbrella header re-exporting the stable surface

src/                    private implementation, mirrors include/ layout
  analyze/
  index/
  query/
  store/

tests/                  GoogleTest, mirrors src/ layout
bench/                  Google Benchmark microbenchmarks
tools/                  CLI binaries (spp_index, spp_search, spp_inspect)
cmake/                  CMake modules, toolchain helpers
docs/                   long-form docs, file-format spec, ADRs
third_party/            vendored sources only when vcpkg cannot host them
```

CLIs in `tools/` are deliberately thin — they exist to exercise the library from the shell and to provide reproducible repros for bugs. New functionality belongs in the library, not in a CLI tool.

## Dataflow: indexing

```
caller
  │
  ▼
spp::index::IndexWriter::AddDocument(Document)
  │
  ├── analyze: Document.fields[*].text  →  TokenStream
  │      (per-field analyzer chain; positions, offsets retained)
  │
  ├── accumulate into an in-memory MutableSegment
  │      (per-term posting lists, per-doc field stats, doc-values)
  │
  └── when MutableSegment exceeds flush threshold:
         seal → encode → write segment files via store
         publish new SegmentInfo via the index manifest (atomic swap)
```

A single `IndexWriter` instance is **not thread-safe** for writes; callers wanting concurrent ingestion fan out across multiple writers writing to separate sub-indexes, or push through a single-producer queue. This is a deliberate simplification — see `DESIGN.md` § "Concurrency".

Sealed segments are immutable. Deletes are tombstoned via a per-segment live-docs bitmap that *is* mutable (under a writer-side lock) and persisted alongside the segment. Background merges compact small segments and physically drop tombstoned docs.

## Dataflow: querying

```
caller
  │
  ▼
spp::query::Searcher::Search(query_string, opts)
  │
  ├── parse:     query_string  →  QueryAst
  │
  ├── analyze:   terms in the AST normalized with the same analyzer
  │              that produced the indexed tokens (pinned per field)
  │
  ├── plan:      QueryAst  →  ScoredIterator tree
  │              (TermIterator, ConjunctionIterator, DisjunctionIterator,
  │               PhraseIterator, FilterIterator)
  │
  ├── execute:   DAAT (document-at-a-time) traversal with WAND/BMW
  │              pruning when the top-K is small
  │
  └── score:     BM25 by default; pluggable Similarity
                 returns a TopK with (doc_id, score, optional explanation)
```

Posting-list iterators are the central abstraction. Every operator in the plan — boolean, phrase, filter, scorer — is an iterator that exposes `advance(target) → doc_id` and `score()`. This is what lets us evaluate large boolean queries without materializing intermediate doc-id sets.

## The Searcher / IndexReader split

`IndexReader` is a snapshot of the index at a point in time: a frozen list of `SegmentReader`s plus the global manifest. Opening a reader takes O(segments) work and is cheap to do repeatedly; readers are reference-counted and segments are mmap'd, so an open reader pins the segment files even if a background merge has logically retired them.

`Searcher` is a thin, throwaway facade over a reader. Construct one per query (or per batch), run searches, drop it. Searchers are not thread-safe; readers are. The intended pattern is: one long-lived `IndexReader` shared across threads, plus a `Searcher` per thread (or per request) constructed on demand.

To pick up new segments after a flush or merge, the caller asks the writer for a fresh `IndexReader`. The previous reader stays valid until dropped; this gives us read-your-writes semantics at the granularity of "the reader I held onto sees the world as of when I opened it".

## File format and segments

The on-disk format is segment-based, modeled loosely on Lucene but simplified. A segment is a directory of files sharing a stem (`segment_<gen>_<id>.*`):

- `.tip` / `.tim` — term dictionary (FST → block-encoded term metadata). **v0.1 implementation note:** the term dictionary is a sorted byte-string list with binary search instead of an FST. The FST upgrade is planned for M5+; the file extensions are reserved so future segments can carry a richer codec without renaming.
- `.doc` — per-term doc-id posting lists (VarByte + skip table)
- `.pos` — per-term position lists (optional; absent if field has positions disabled)
- `.fdt` / `.fdx` — stored fields (compressed, chunked)
- `.dvd` — doc-values (columnar, one stripe per field)
- `.liv` — live-docs bitmap (only file in a sealed segment that may change)
- `.si`  — segment info: schema version, analyzer fingerprint, doc count, codec versions

Cross-cutting concerns live in `store/codec`: each codec is versioned and selected via the segment's `.si`. This is how we evolve the format without rewriting old segments — readers carry codecs for every version they need to read, writers only ever emit the current version.

## Concurrency model in one paragraph

One writer, many readers, immutable segments. Writes go through a single `IndexWriter` that owns mutable in-memory state and a write lock on the index directory. Readers see a snapshot via an `IndexReader`; multiple readers and multiple `Searcher`s can run in parallel against the same reader without locks because the segments they touch are immutable. The only mutable on-disk state is the per-segment live-docs bitmap, which uses atomic file replacement (write to tmp, fsync, rename) so readers see either the old or new bitmap, never a torn write. Background merges produce new segments and publish them via an atomic manifest swap — they never modify existing segments. See `DESIGN.md` § "Concurrency and segment immutability" for the load-bearing invariants.

## Cross-cutting concerns

- **Errors.** Hot paths (query execution, posting-list decode) are non-throwing; they return `std::expected` or use sentinel doc-ids. Setup paths (opening a reader, parsing a query, building a writer) may use exceptions for I/O and parse errors.
- **Logging.** Stdlib `std::format`-style structured logging via a single sink in `spp::log`. The library does not write to stdout/stderr by default; embedders install a sink.
- **Metrics.** Per-`Searcher` counters (iterations, comparisons, scoring calls) are surfaced via an explanation API; there is no global metrics registry inside the library.
- **Threading.** The library spawns threads only for explicitly opt-in subsystems (background merger, async warmup). All such threads are owned by an `Executor` the embedder injects — there is no default thread pool.

## Where things will *not* live

- No HTTP/gRPC server in this repo. A server lives in a sibling repo and depends on `spp` as a library.
- No language bindings (Python, Go) in this repo for the same reason.
- No persistence of query-side state (caches across processes). A query cache, if added, is per-`IndexReader` and dies with it.
