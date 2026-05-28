#!/usr/bin/env bash
# hello_search — minimal end-to-end use of the SearchPlusPlus HTTP API.
#
# Mirrors examples/hello_search/main.cpp line-for-line: same corpus, same
# queries, same printed output — but instead of linking the C++ library, this
# talks to a `spp_serve` instance over HTTP using only `curl` and `python3`.
#
# Build & run (from the repo root):
#   cmake --preset default
#   cmake --build --preset default --target spp_serve
#   ./examples/http/hello_search.sh

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

# Locate spp_serve. Prefer release, fall back to default.
SERVE=""
for candidate in \
    "${REPO_ROOT}/build/release/tools/spp_serve/spp_serve" \
    "${REPO_ROOT}/build/default/tools/spp_serve/spp_serve"; do
    if [[ -x "${candidate}" ]]; then
        SERVE="${candidate}"
        break
    fi
done
if [[ -z "${SERVE}" ]]; then
    echo "hello_search: spp_serve binary not found." >&2
    echo "  build it with: cmake --build --preset default --target spp_serve" >&2
    exit 1
fi

PORT="${SPP_PORT:-9200}"
BASE="http://127.0.0.1:${PORT}"
INDEX_DIR="$(mktemp -d -t spp_hello_http.XXXXXX)"
SERVER_PID=""
SERVER_LOG="${INDEX_DIR}/server.log"

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    rm -rf "${INDEX_DIR}"
}
trap cleanup EXIT

"${SERVE}" --index "${INDEX_DIR}" --host 127.0.0.1 --port "${PORT}" \
    >"${SERVER_LOG}" 2>&1 &
SERVER_PID=$!

# Wait for /_health to come up (up to ~5s).
ready=0
for _ in $(seq 1 50); do
    if curl -fsS -o /dev/null "${BASE}/_health"; then
        ready=1
        break
    fi
    sleep 0.1
done
if (( ! ready )); then
    echo "hello_search: spp_serve did not become ready on ${BASE}" >&2
    echo "--- server log ---" >&2
    cat "${SERVER_LOG}" >&2
    exit 1
fi

# Create index `wiki` with two text fields.
curl -fsS -o /dev/null -X PUT "${BASE}/wiki" \
    -H 'Content-Type: application/json' \
    -d '{"mappings":{"title":{"type":"text"},"body":{"type":"text"}}}'

post_doc() {
    local id="$1" title="$2" body="$3"
    python3 -c '
import json, sys
print(json.dumps({"_id": sys.argv[1], "title": sys.argv[2], "body": sys.argv[3]}))
' "${id}" "${title}" "${body}" | \
        curl -fsS -o /dev/null -X POST "${BASE}/wiki/_doc" \
            -H 'Content-Type: application/json' --data-binary @-
}

post_doc a "Search engines"    "How full-text engines tokenize, index, and rank."
post_doc b "Inverted index"    "An inverted index maps each term to the docs containing it."
post_doc c "BM25 explained"    "BM25 scores relevance via term frequency and document length."
post_doc d "Posting lists"     "Posting lists store doc ids and term frequencies."
post_doc e "Tokenizer basics"  "A tokenizer splits text into the tokens the index stores."

curl -fsS -o /dev/null -X POST "${BASE}/wiki/_refresh"

run_query() {
    local query="$1" field="$2"
    local q_enc field_enc
    q_enc="$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "${query}")"
    field_enc="$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "${field}")"
    curl -fsS "${BASE}/wiki/_search?q=${q_enc}&default_field=${field_enc}&size=5" \
        | python3 -c '
import json, sys
query = sys.argv[1]
data = json.loads(sys.stdin.read())
total = data["total"]
print("%-24s total=%d" % (query, total))
for hit in data.get("hits", []):
    print("    %s   score=%.4f" % (hit["_id"], hit["_score"]))
' "${query}"
}

run_query "inverted index"    "body"
run_query "bm25"              "body"
run_query "title:tokenizer"   "title"
