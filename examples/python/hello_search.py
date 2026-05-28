"""hello_search — minimal end-to-end use of the SearchPlusPlus Python bindings.

Mirrors examples/hello_search/main.cpp line-for-line: same corpus, same
queries, same printed output. Useful as a quick sanity check that the
extension module imports and the underlying engine is reachable.

Build (from the repo root):
  cmake --preset default -DSPP_BUILD_PYTHON=ON
  cmake --build --preset default --target searchplusplus

Run:
  PYTHONPATH=build/default/python python3 examples/python/hello_search.py
"""

from __future__ import annotations

import tempfile

import searchplusplus as spp


CORPUS: list[tuple[str, str, str]] = [
    ("a", "Search engines", "How full-text engines tokenize, index, and rank."),
    ("b", "Inverted index", "An inverted index maps each term to the docs containing it."),
    ("c", "BM25 explained", "BM25 scores relevance via term frequency and document length."),
    ("d", "Posting lists", "Posting lists store doc ids and term frequencies."),
    ("e", "Tokenizer basics", "A tokenizer splits text into the tokens the index stores."),
]

QUERIES: list[tuple[str, str]] = [
    ("inverted index", "body"),
    ("bm25", "body"),
    ("title:tokenizer", "title"),
]


def main() -> int:
    schema = spp.Schema()
    schema.add_text_fields(["title", "body"])

    with tempfile.TemporaryDirectory(prefix="spp_hello_py_") as tmp:
        writer = spp.IndexWriter.open(tmp, initial_schema=schema)

        for doc_id, title, body in CORPUS:
            doc = spp.Document()
            doc.id = doc_id
            doc.fields = {"title": title, "body": body}
            writer.add_document(doc)
        writer.refresh()

        searcher = spp.Searcher(writer.current_reader())
        for query, field in QUERIES:
            result = searcher.search(query, default_field=field, size=5)
            print(f"{query:<24} total={result.total_hits}")
            for hit in result.hits:
                print(f"    {hit.id}   score={hit.score:.4f}")

        writer.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
