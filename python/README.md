# Python bindings for SearchPlusPlus

A small [pybind11](https://github.com/pybind/pybind11) layer that exposes
`spp::index::Schema`, `Document`, `IndexWriter`, `IndexReader`, and
`spp::query::Searcher` to Python. Python-side methods are `snake_case`;
errors that the C++ side returns via `Status` / `Expected<T>` are raised
as Python exceptions (`ValueError`, `KeyError`, `IndexError`, or
`RuntimeError` depending on the underlying `StatusCode`).

The bindings live in `python/src/searchplusplus.cpp` and build into a
single CPython extension module named `searchplusplus`.

## Build

The bindings are gated by a CMake option that's **off by default**, so
the standard build doesn't require Python or pybind11:

```bash
# Requires Python 3 development headers + pybind11
#   macOS:    brew install pybind11
#   Ubuntu:   apt-get install python3-dev pybind11-dev
#   vcpkg:    add "pybind11" to vcpkg.json
cmake --preset release -DSPP_BUILD_PYTHON=ON
cmake --build --preset release --target searchplusplus -j
```

The output `.so` lands in `build/release/python/`.

### ⚠️  Don't build the bindings with sanitizers on

The `default` preset enables AddressSanitizer + UBSan. ASan needs to be
initialized at process startup, but a Python extension is `dlopen`'d
into a Python interpreter that wasn't launched with ASan — at which
point ASan errors out with *"Interceptors are not working"*. Build the
bindings under the `release` preset (as above), or pass
`-DSPP_ENABLE_SANITIZERS=OFF` if you need a non-release build.

## Use

The example reproduces `examples/hello_search/main.cpp` in Python:

```bash
PYTHONPATH=build/release/python python3 examples/python/hello_search.py
```

(Use the same Python interpreter whose headers built the `.so` —
the filename embeds the ABI tag, e.g. `searchplusplus.cpython-314-darwin.so`
will not load into Python 3.13.)

## API surface

```python
import searchplusplus as spp

schema = spp.Schema()
schema.add_text_fields(["title", "body"])

writer = spp.IndexWriter.open("/path/to/index", initial_schema=schema)
doc = spp.Document()
doc.id = "a"
doc.fields = {"title": "Hello", "body": "world"}
writer.add_document(doc)
writer.refresh()

searcher = spp.Searcher(writer.current_reader())
result = searcher.search("hello", default_field="body", size=10)
for hit in result.hits:
    print(hit.id, hit.score)
```

The binding deliberately covers only the v0.1 surface (schema, writer,
searcher). LTR ranker registration and the per-token-weight / quality
features from v0.2 are not wired up yet — open an issue if you need
them and they're a small extension to add.
