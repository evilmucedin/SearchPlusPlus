#!/usr/bin/env bash
# demo_bench_search.sh — end-to-end search benchmark with checked-in data.
#
# Spins up spp_serve against a temp index, loads the 30-doc demo corpus over
# HTTP, then runs bench_search.py for a short duration so you can verify the
# pipeline before pointing it at a real index.
#
# Intent: pipeline smoke test. The demo corpus is tiny — absolute numbers
# here don't represent production throughput. Use this script as a template
# for running bench_search.py against your own index.
#
# Prereqs:
#   cmake --preset default
#   cmake --build --preset default --target spp_serve -j

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

SERVE=""
for c in \
    "build/release/tools/spp_serve/spp_serve" \
    "build/default/tools/spp_serve/spp_serve"; do
    if [[ -x "${c}" ]]; then
        SERVE="${c}"
        break
    fi
done
if [[ -z "${SERVE}" ]]; then
    echo "demo_bench_search: spp_serve not built." >&2
    echo "  cmake --preset default" >&2
    echo "  cmake --build --preset default --target spp_serve -j" >&2
    exit 1
fi

PORT="${SPP_PORT:-9211}"
BASE="http://127.0.0.1:${PORT}"
DURATION="${DURATION:-15}"
CONCURRENCY="${CONCURRENCY:-16}"
INDEX_DIR="$(mktemp -d -t spp_demo_bench.XXXXXX)"
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

# Wait for /_health.
ready=0
for _ in $(seq 1 50); do
    if curl -fsS -o /dev/null "${BASE}/_health" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if (( ! ready )); then
    echo "demo_bench_search: spp_serve did not become ready on ${BASE}" >&2
    echo "--- server log ---" >&2
    cat "${SERVER_LOG}" >&2
    exit 1
fi

# Create index using the demo mapping.
curl -fsS -o /dev/null -X PUT "${BASE}/bench" \
    -H 'Content-Type: application/json' \
    --data-binary @data/demo_mapping.json

# Stream the demo corpus into the server: each line is already a JSON doc.
# The HTTP API expects `_id` rather than the corpus's `id`, so rewrite on the fly.
python3 - "${BASE}" <<'PY'
import json, sys, urllib.request
base = sys.argv[1]
with open("data/demo_corpus.jsonl", "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        doc = json.loads(line)
        if "id" in doc and "_id" not in doc:
            doc["_id"] = doc.pop("id")
        body = json.dumps(doc).encode("utf-8")
        req = urllib.request.Request(
            f"{base}/bench/_doc", data=body, method="POST",
            headers={"Content-Type": "application/json"},
        )
        urllib.request.urlopen(req).read()
PY

curl -fsS -o /dev/null -X POST "${BASE}/bench/_refresh"

echo "demo_bench_search: driving load for ${DURATION}s at concurrency=${CONCURRENCY}"
python3 scripts/bench_search.py \
    --url "${BASE}" --index bench \
    --duration "${DURATION}" --concurrency "${CONCURRENCY}" \
    --warmup 1

echo
echo "demo_bench_search: pipeline OK."
echo "  (numbers above are from a 30-doc corpus — not a production throughput baseline.)"
