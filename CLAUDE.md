# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

SearchPlusPlus is a **C++ full-text search engine library** in the very early bootstrap phase. At the time of writing, the repository contains only this file, `ARCHITECTURE.md`, `DESIGN.md`, `LICENSE`, and `README.md` — there is no source code yet. The intended shape of the system lives in `ARCHITECTURE.md` (component layout, dataflow) and `DESIGN.md` (design decisions, rationale). Treat those documents as the source of truth for how new code should be organized; if a proposed change conflicts with them, update them as part of the change rather than silently diverging.

## Founding principles (read these before proposing anything substantial)

These are the project's anchors. If a change you're about to suggest tugs against one of them, surface the tradeoff explicitly — don't quietly accept it.

1. **Performance is the headline goal.** It is the reason this codebase is in C++. Trade ergonomics for speed on the indexing and query hot paths.
2. **No strong dependencies.** Default answer to "should we add `<library>`?" is *no*. The bar is "doing it ourselves would be a project of its own" *and* the library is small, focused, well-maintained. The current allowed list is in `DESIGN.md` § "Dependencies"; do not silently widen it.
3. **Linux is primary; Windows and macOS are supported.** Performance is tuned on Linux. No Linux-only syscall appears outside `store/platform/`. CI builds and runs the test suite on all three platforms.
4. **Tests and benchmarks are first-class.** A feature without unit tests, an integration test, and a benchmark for any hot path it touches is incomplete. Regressions on tracked benchmarks block PRs unless explicitly accepted.
5. **English first, other languages supported.** Default analyzer chain is tuned for English; non-English support is genuine (Unicode-correct via ICU, per-field analyzer chains) but English quality and performance come first.

## Build & test (once the scaffold lands)

The build system will be CMake ≥ 3.20 with vcpkg manifest mode and CMakePresets. Until the scaffold is committed these commands will not work — use them as the canonical form once `CMakeLists.txt` exists.

```bash
# Configure (default preset = Debug, vcpkg toolchain)
cmake --preset default

# Build
cmake --build --preset default -j

# Run the full test suite
ctest --preset default --output-on-failure

# Run a single test by name (GoogleTest filter)
ctest --preset default -R InvertedIndexTest --output-on-failure
# Or directly:
./build/default/tests/spp_tests --gtest_filter='InvertedIndexTest.*'

# Benchmarks (Google Benchmark)
./build/default/bench/spp_bench --benchmark_filter=BM_PostingListIntersect

# Lint / format
clang-format -i $(git ls-files '*.cpp' '*.h' '*.hpp')
clang-tidy -p build/default <file>
```

A `release` preset (RelWithDebInfo, LTO on) and an `asan` preset (Debug + ASan/UBSan) should be added alongside `default`.

## Layout conventions

- Public API headers live under `include/spp/<module>/...` and are reachable as `#include <spp/module/header.h>`. Anything inside `src/` is private — do not `#include` across module boundaries via relative paths.
- Top-level namespace is `spp`. Each module gets a nested namespace (`spp::index`, `spp::query`, `spp::store`, `spp::analyze`). Internal helpers go under `spp::<module>::detail`.
- One class or one cohesive set of free functions per header. File names are `snake_case.h` / `snake_case.cpp`.
- Tests mirror the source tree: `src/index/inverted_index.cpp` → `tests/index/inverted_index_test.cpp`. Each test target links against the library, not against individual translation units.
- Benchmarks live in `bench/` and follow the same mirroring rule with a `_bench.cpp` suffix.

## Code style

- C++20. Prefer `std::span`, `std::string_view`, `[[nodiscard]]`, designated initializers, and concepts where they improve clarity. Avoid coroutines and modules until the build matrix supports them on all target compilers.
- No exceptions on the hot path (query execution, posting-list decode). Use `std::expected` (or a vendored equivalent until C++23 is universal) for recoverable errors and `SPP_CHECK` for invariants. Constructors that can fail should expose a static `Create(...)` factory returning `std::expected`.
- Ownership: prefer values and `std::unique_ptr`. `std::shared_ptr` only for genuinely shared lifetimes (e.g. immutable segments referenced by concurrent readers).
- Allocations on the query hot path must be opt-in. Use arena/`pmr` allocators for transient query state.
- Public headers must compile cleanly under `-Wall -Wextra -Wpedantic -Wconversion`. Treat warnings as errors in CI.

## Working with the architecture

When adding a feature, identify which **layer** it belongs to before writing code — the layers are intentionally one-directional:

```
analyze  →  index  →  store
                ↑
query  ─────────┘
```

`query` depends on `index` and `analyze`; `index` depends on `store` and `analyze`; `store` depends on nothing inside `spp`. A new feature that needs to reach "upward" (e.g. `store` calling into `query`) is almost always a design smell — surface it in the PR description so it can be discussed against `DESIGN.md` rather than merged silently.

Segments are **immutable** once sealed. Any change that mutates a sealed segment in place violates a load-bearing invariant of the concurrency model (see `DESIGN.md` § "Concurrency and segment immutability"). Adding a "just this once" mutation path is not acceptable without revisiting that section first.

## What not to do

- Do not add a dependency without updating `vcpkg.json` and noting the rationale in `DESIGN.md` § "Dependencies". The dependency budget is deliberately small (principle 2).
- Do not introduce a Linux-only code path outside `store/platform/`. If you genuinely need one, add it behind a portable fallback and put the Linux specialization in the platform shim (principle 3).
- Do not land a hot-path change without a benchmark (or the explanation of why no hot path was touched). Do not land any change without tests (principle 4).
- Do not bake English-only assumptions ("a token is an ASCII word", "lowercasing is `tolower`") into core code. English-first means tuned for English at the analyzer layer, not English-only at the engine layer (principle 5).
- Do not introduce a new top-level directory without updating `ARCHITECTURE.md`.
- Do not silently widen the public API surface. Anything moved into `include/spp/` is a commitment; prefer keeping things in `src/` until a second caller actually needs them.
