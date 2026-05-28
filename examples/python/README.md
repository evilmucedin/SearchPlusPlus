# Python examples

| Example | What it shows |
|---|---|
| [`hello_search.py`](hello_search.py) | Line-for-line Python mirror of `examples/hello_search/main.cpp`. Same corpus, same queries, same printed output — useful as a smoke test that the bindings load and the engine is reachable. |

Build instructions and the sanitizer caveat live in
[`python/README.md`](../../python/README.md).

Run from the repo root:

```bash
cmake --preset release -DSPP_BUILD_PYTHON=ON
cmake --build --preset release --target searchplusplus -j
PYTHONPATH=build/release/python python3 examples/python/hello_search.py
```
