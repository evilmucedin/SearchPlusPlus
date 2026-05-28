# Design

This document captures the **why** behind SearchPlusPlus. `ARCHITECTURE.md` describes the shape; this file explains the decisions that shaped it and the tradeoffs taken. Each section is a load-bearing decision — if a change conflicts with what is written here, update this document in the same change rather than letting code and rationale drift apart.

## Founding principles

These five principles were stated at project kickoff and anchor every decision that follows. When a proposed change tugs against one of them, the principle wins by default; reversing one is a discussion to have explicitly, not a thing to drift into.

1. **Performance is the headline goal.** It is why this project is in C++ rather than a language with a friendlier developer experience. When forced to choose between clarity and a measurable speedup on the indexing or query hot path, we choose the speedup — and we pay the clarity cost back with comments and tests, not by softening the constraint.
2. **No strong dependencies.** Every external library is a permanent commitment we are inflicting on every embedder. The default answer to "should we add `<library>`?" is *no*. We accept a dependency only when (a) doing the work ourselves would be a project of its own, and (b) the library is focused, well-maintained, and small in surface area. Sprawling frameworks (Boost, the full Abseil) are out; tight single-purpose libraries (CRoaring, ICU for what only ICU can do) are negotiable.
3. **Linux is primary; Windows and macOS are supported.** The engine is tuned on Linux and Linux performance regressions are taken most seriously. Windows and macOS are first-class for *correctness* — they are in CI from day one and no Linux-only syscall is allowed in the portable layer — but performance work happens on Linux first and is verified on the other two afterwards.
4. **Tests and benchmarks are first-class.** A feature without unit tests, an integration test for the realistic round-trip, and a benchmark for the hot path it touches is incomplete. Tracked benchmarks are part of the contract: a meaningful regression blocks the PR unless explicitly accepted with a documented reason. Fuzzing covers everything that parses bytes.
5. **English first, other languages supported.** The default analyzer chain is tuned for English (tokenization, stemming, stop words). Non-English support is genuine, not lip service — Unicode-correct normalization and segmentation via ICU, per-field analyzer chains, no assumption that "a token is an ASCII word" anywhere in the core — but English is where we optimize quality and performance first.

The remainder of this document is the *application* of these principles. If a section below contradicts one of them, the section is wrong.

## Language and standard

**C++20**, not C++17 or C++23.

- C++17 lacks `std::span`, concepts, designated initializers, and `<bit>` — all of which materially simplify the iterator-heavy query layer and the bit-twiddling in posting-list codecs.
- C++23 (`std::expected`, `std::flat_map`, modules, std `mdspan`) is genuinely useful but not yet uniformly available on the compilers we want to support (older GCC and Clang on long-tail Linux distros). We vendor a small `expected` shim today and switch to `std::expected` once the floor moves.
- Coroutines and modules are off the table for the foreseeable future. Coroutines complicate the no-allocation hot path and modules are still a build-system minefield in CMake/vcpkg.

Supported compilers: GCC ≥ 12, Clang ≥ 15, MSVC ≥ 19.34. CI matrix builds on all three.

## Platforms

Per principle 3, **Linux is the primary platform** and Windows / macOS are supported. Operationally this means:

- **Linux (x86_64, glibc).** Primary development and tuning target. Performance numbers in benchmarks and design discussions are Linux numbers unless explicitly stated otherwise. A regression on the Linux benchmark suite is a regression, full stop.
- **macOS (arm64 and x86_64).** First-class for correctness. Full CI build + test on every PR. ARM-specific SIMD paths (NEON) sit alongside the x86 paths (SSE4.2 / AVX2 / AVX-512 where available) behind the same runtime-dispatched interface.
- **Windows (x86_64, MSVC).** First-class for correctness. Full CI build + test on every PR. We do not require feature parity for performance tunables that depend on Linux-specific APIs (`io_uring`, `madvise(MADV_POPULATE_READ)`, `perf_event_open`); those features are off by default on Windows with the portable fallback active.

Code that needs platform-specific behavior lives behind a small `store/platform/` shim with a portable fallback. No Linux-only system call appears outside that directory. Path handling uses `std::filesystem` end-to-end; we never assume `/` separators in serialized paths.

CI runs the test suite on all three platforms for every PR. Benchmarks run on Linux on every PR and on macOS/Windows nightly.

## Build system: CMake + vcpkg

CMake is non-negotiable for a C++ library that wants to be embedded; vcpkg in manifest mode (`vcpkg.json` in the repo) is the path of least resistance for reproducible dependency builds without forcing the embedder onto a particular package manager. Conan is a viable alternative but adds a second dependency model alongside whatever the embedder already uses; vcpkg's "manifest in tree, lockfile checked in, toolchain file does the rest" story is simpler. CMakePresets pins the configurations so `cmake --preset default` works identically on every developer's machine and in CI.

We prefer system-package-style FindModules only as a last resort. New dependencies should be added to `vcpkg.json` and consumed via `find_package`.

## Dependencies

The dependency budget is small on purpose — see principle 2. Each dependency below is justified individually. Anything not on this list is not allowed without first appearing here.

**Runtime dependencies of the library:**

- **ICU** — Unicode normalization (NFKC), case folding, language-aware word segmentation, locale-aware collation. There is no remotely competitive alternative for correctness across scripts, and writing it ourselves would be a multi-year project. ICU is a heavy dependency, so it is gated behind a CMake option (`SPP_WITH_ICU`, default ON). With it OFF, the analyzer falls back to an ASCII-only tokenizer that is correct for English and degrades for everything else — useful for embedders who can guarantee English-only input and want to avoid the binary-size cost.
- **CRoaring** — compressed bitmaps for live-docs, filter sets, and very-low-density posting lists. Single-purpose, ~10kLOC, plain C, drop-in. Replacing it would be a focused but real project.

**Build-time / dev-only dependencies (do not appear in the embedder's link line):**

- **GoogleTest** for unit and integration tests.
- **Google Benchmark** for microbenchmarks.
- **libFuzzer** (via the compiler's sanitizer suite) for nightly fuzzing.

**Header-only utilities** are preferred over heavy framework dependencies whenever the choice is available. Where C++20 / C++23 cover what we need (`std::span`, `<format>` once the floor moves, `<bit>`, `std::expected` via a tiny vendored shim until C++23 is universal), we use the standard library.

**Explicitly rejected:**

- **Boost** — too heavy, forces a transitive dependency on every embedder.
- **Abseil** — we considered it for `flat_hash_map` and string utilities, but the surface area is enormous and pulling in any of it tends to pull in much of it. We use a small header-only hash map (`ankerl::unordered_dense` or equivalent) where `std::unordered_map` is too slow, and write the handful of string helpers we need.
- **fmt** as a runtime dependency — we use `<format>` from C++20 with a tiny vendored shim for compilers that lag, instead of carrying fmt forever.
- **spdlog** — we own our log abstraction so embedders can install their own sink. A dependency would invert that control.
- **protobuf / flatbuffers / Cap'n Proto** — the on-disk format is hand-rolled, see § "Why a hand-rolled file format".

Adding a new dependency requires (1) a new bullet in this list with the same justification depth, (2) an entry in `vcpkg.json` (or a `cmake/Find<Lib>.cmake` if it cannot be vcpkg-hosted), and (3) the new dependency's license verified against the repo's. The bar is the bar from principle 2: doing it ourselves would be a project of its own *and* the library is focused enough that we can reasonably absorb its cost.

## Why a hand-rolled file format

The segment format is hand-rolled rather than layered on protobuf, flatbuffers, or Cap'n Proto. The reasons:

1. **Decode is the hot path.** Posting-list iteration is dominated by integer decode (VarByte / PForDelta). A general-purpose serialization library would either force a memcpy into a generated struct or force us to bypass it for the hot bits anyway. We bypass it directly.
2. **The schema barely evolves.** A search engine's on-disk format changes on the order of once a year. The flexibility of a schema language is overkill; the cost — opaque binary layout, generated headers in the build — is permanent.
3. **mmap-friendliness matters.** We want segment files to be readable by pointing into mmap'd memory with no parsing step for the bulk data. That argues for fixed layouts with explicit offsets, which is what we'd end up doing on top of a serialization library anyway.

A versioned envelope (`.si`) carries the codec version for every component, so format evolution is per-codec rather than per-segment-format. See `ARCHITECTURE.md` § "File format and segments".

## Concurrency and segment immutability

The concurrency model is **one writer, many readers, immutable segments**. This is the single most load-bearing decision in the system. Most other simplifications cascade from it.

**Invariants** (do not break without revisiting this section):

1. A sealed segment's data files (`.tip`, `.tim`, `.doc`, `.pos`, `.fdt`, `.fdx`, `.dvd`, `.si`) are never modified after they are visible to a reader. They may be deleted only after every reader that could see them has been dropped.
2. The only mutable file in a sealed segment is `.liv` (live-docs). It is updated by atomic rename: write to a temp file, fsync, rename over the old one. Readers either see the old bitmap or the new one — never a torn read.
3. The set of segments comprising the index is published via a manifest file. Adding or removing segments is an atomic-rename swap of the manifest. An `IndexReader` reads the manifest once at open time and pins exactly those segments.
4. There is at most one `IndexWriter` per index directory at any time, enforced by an exclusive lock file (`write.lock`). This is a process-level lock; we do not attempt to coordinate writers across processes via anything more sophisticated.

**Why not many writers?** Multi-writer designs (per-thread in-memory buffers that periodically merge into a shared structure) are doable but they push complexity into every part of the indexing path — concurrent term-dictionary updates, lock-free posting-list builders, ordering guarantees during flush. The single-writer model trades that complexity for a small ingestion-throughput ceiling on a single index; embedders who need more throughput shard at a higher level (multiple indexes, each with its own writer).

**Tradeoff accepted.** Visibility is near-real-time, not real-time. A document added now is visible after the writer flushes the in-memory segment, the manifest is swapped, and the caller obtains a fresh reader. For sub-second freshness, callers can lower the flush threshold and explicitly refresh.

## Scoring: BM25 by default, Similarity is pluggable

BM25 is the default for the same reason every other modern engine defaults to it: it works well across domains, has two understandable knobs (`k1`, `b`), and degrades gracefully as documents get longer. TF-IDF is supported as a Similarity for compatibility but not recommended. The Similarity interface is small enough — `idf(term_stats)`, `score(tf, norm)`, `combine(...)` — that adding a custom scorer is a contained change.

**Learning-to-rank and neural rerankers are out of scope** for the library. Embedders can take the top-K and rerank externally; baking a model runtime into the library would invert the dependency budget.

## Query model: boolean + phrase, parsed from a string

The query language is a small superset of Lucene's: terms, phrases, conjunctions, disjunctions, must-not, field qualifiers, range queries, prefix and wildcard, with explicit grouping. We chose a string-based DSL over a structured builder for two reasons: (1) every embedder eventually wants to accept user-typed queries, so the parser has to exist anyway; (2) one canonical representation avoids a second, parallel API surface.

A programmatic builder is exposed as a secondary API for embedders constructing queries from structured input — internally it produces the same `QueryAst`.

## Iterator-centric execution

Every operator in the query plan is a `DocIdSetIterator` with the same shape:

```cpp
class DocIdSetIterator {
 public:
  virtual DocId next() = 0;            // advance one
  virtual DocId advance(DocId t) = 0;  // skip to >= t
  virtual DocId docID() const = 0;
  virtual int64_t cost() const = 0;    // for planner
};
```

This is what enables `WAND` / `BlockMaxWAND` pruning for top-K: the planner orders iterators by max-score contribution, advances the cheapest one first, and skips documents that cannot reach the top-K threshold. The cost of this uniformity is some virtual-call overhead per advance; in practice the branch predictor handles it well and the cache behavior of the underlying posting-list reads dominates.

## Error handling split

Errors are split into two regimes:

- **Setup-time errors** (opening an index, parsing a query, building a writer) use exceptions. These are rare, the caller is going to handle them at a coarse boundary, and the cost of unwinding is irrelevant compared to the I/O that already failed.
- **Hot-path errors** (decoding a posting, scoring a hit) do not throw. They return `std::expected` or use sentinel doc-ids (`kNoMoreDocs`). Throwing per-document would dwarf the actual query work.

`SPP_CHECK` is used for invariants that we believe cannot fail and that should crash loudly if they do (segment file corruption detected mid-decode, an iterator advanced past `kNoMoreDocs`). It is not a substitute for input validation.

## Ownership and allocation

- Values and `std::unique_ptr` are the defaults. `std::shared_ptr` is used only where lifetimes are genuinely shared and the count is dynamic — segment readers held by both a live `IndexReader` and an in-flight merge are the canonical case.
- Hot-path allocations are opt-in. Query execution allocates against a per-search arena (`std::pmr::monotonic_buffer_resource` backed by a small stack buffer and a heap fallback). Iterators that need scratch space ask the arena, not `new`.
- We do not use a custom global allocator. Embedders may, and the library must not assume system `malloc`.

## Threading: injected, not owned

The library does not spawn threads on its own. Background work — segment merges, async warmup, optional prefetch — is submitted to an `Executor` interface that the embedder implements. A default in-process `Executor` backed by `std::jthread` is provided for convenience, but it lives in `tools/` so a linker pulling in `spp` alone does not get it. This is the same rationale as the logging sink: a library that secretly owns threads is a poor citizen inside a host process that has opinions about its scheduler.

## Testing strategy

Per principle 4, tests and benchmarks are part of the deliverable, not a separate workstream. A feature lands with all three of: unit tests, an integration test for the realistic round-trip it enables, and a benchmark for the hot path it touches (or a documented reason no hot path was touched).

Four layers, each runnable in isolation:

1. **Unit tests** — one test target per module, covering the public surface of that module against fakes for adjacent modules. Fast; the full unit suite should finish in seconds. Targets are sized so a developer can re-run the relevant subset on every save.
2. **Integration tests** — exercise full index/search round-trips against on-disk segments: ingest, flush, reopen, merge, tombstone, crash-recovery. These build real segments on a temp directory and are the ones that catch file-format and concurrency regressions.
3. **Property tests** for combinatorial input spaces: query parsing (round-trip parse → print → parse), codec round-trips (encode → decode equals input for every codec at every supported version), merge orderings, analyzer idempotence.
4. **Fuzzing** (libFuzzer, run nightly and on a per-commit smoke budget) against every byte-eating surface: query parser, each codec decoder, the segment-file reader. New parser or codec code must land with a corresponding fuzz target.

**Benchmarks are gated.** A representative set lives in `bench/` and runs on Linux in CI on every PR (per principle 3, Linux is where regressions are taken most seriously). Tracked benchmarks include at minimum: `BM_QueryLatency_TopK10`, `BM_QueryLatency_PhraseTopK10`, `BM_IndexingThroughput`, `BM_PostingListDecode`, `BM_SegmentMerge`. A meaningful regression — bigger than the run-to-run noise band documented for that benchmark — blocks the PR unless the author lands an explicit, named accept-regression note explaining the tradeoff. "I don't know why it regressed" is not a sufficient note.

**Coverage targets.** We aim for line coverage ≥ 90% on `src/` excluding obvious-impossible-branch error paths, and branch coverage ≥ 80%. These are floors to keep us honest, not ceilings — the value is in whether the tests *catch the bugs we actually have*, not in the number.

**ASan / UBSan / TSan.** Every PR runs the unit suite under AddressSanitizer + UndefinedBehaviorSanitizer on Linux. ThreadSanitizer runs nightly on the integration suite. Failures block.

## What this document is not

This is not a backlog. Open questions and "ideas we might want someday" belong in `docs/adr/` (architecture decision records, one file per decision) once that directory exists. This document only contains decisions that have been made and that current code relies on.
