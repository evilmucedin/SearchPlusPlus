# HTTP examples

| Example | What it shows |
|---|---|
| [`hello_search.sh`](hello_search.sh) | Same corpus and queries as the C++ and Python `hello_search` examples, but driven entirely through `spp_serve`'s HTTP API with `curl`. The script launches `spp_serve` on a temporary index, creates the index, posts five documents, refreshes, runs three queries, prints the same aligned output, then tears everything down. |

Prerequisites:

- `spp_serve` built in either the `release` or `default` preset's `tools/spp_serve/` directory (the script auto-detects).
- `curl` and `python3` on `PATH` (Python is used only to URL-encode query strings and pretty-print JSON; no SearchPlusPlus Python module needed).
- TCP port `9200` free, or override with `SPP_PORT=<port>`.

Run from the repo root:

```bash
cmake --preset release
cmake --build --preset release --target spp_serve -j
./examples/http/hello_search.sh
```

Expected output (matches `examples/hello_search/main.cpp` and `examples/python/hello_search.py` byte-for-byte):

```
inverted index           total=1
    b   score=1.9253
bm25                     total=1
    c   score=1.3863
title:tokenizer          total=1
    e   score=1.3863
```

The script's body is the easiest place to read off the API shape: `PUT /:index`
with a `mappings` body, `POST /:index/_doc` per document, `POST
/:index/_refresh` to seal a generation, `GET /:index/_search?q=...` to query.
