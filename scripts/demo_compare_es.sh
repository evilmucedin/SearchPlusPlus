#!/usr/bin/env bash
# demo_compare_es.sh — side-by-side comparison of SearchPlusPlus and ES.
#
# Brings up an ES container (Docker), starts spp_serve, ingests the demo
# corpus into both, then runs scripts/compare_es.py for quality and
# throughput numbers.
#
# Prereqs:
#   - Docker daemon running (`colima start` on macOS).
#   - cmake --build --preset default --target spp_serve

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
    echo "demo_compare_es: spp_serve not built." >&2
    echo "  cmake --build --preset default --target spp_serve -j" >&2
    exit 1
fi

ES_PORT="${ES_PORT:-9200}"
SPP_PORT="${SPP_PORT:-9211}"
ES_CONTAINER="${ES_CONTAINER:-spp-es-compare}"
ES_IMAGE="${ES_IMAGE:-docker.elastic.co/elasticsearch/elasticsearch:8.13.4}"
DURATION="${DURATION:-10}"
CONCURRENCY="${CONCURRENCY:-8}"

INDEX_DIR="$(mktemp -d -t spp_demo_compare.XXXXXX)"
SERVER_PID=""
SERVER_LOG="${INDEX_DIR}/spp.log"
STARTED_ES_HERE=0

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    if (( STARTED_ES_HERE == 1 )); then
        docker rm -f "${ES_CONTAINER}" >/dev/null 2>&1 || true
    fi
    rm -rf "${INDEX_DIR}"
}
trap cleanup EXIT

# --- ElasticSearch ---
if curl -fsS -o /dev/null "http://127.0.0.1:${ES_PORT}/_cluster/health" 2>/dev/null; then
    echo "demo_compare_es: reusing ES already on port ${ES_PORT}"
else
    echo "demo_compare_es: starting ES (${ES_IMAGE}) on port ${ES_PORT}"
    docker rm -f "${ES_CONTAINER}" >/dev/null 2>&1 || true
    docker run -d --name "${ES_CONTAINER}" \
        -p "${ES_PORT}:9200" \
        -e "discovery.type=single-node" \
        -e "xpack.security.enabled=false" \
        -e "ES_JAVA_OPTS=-Xms1g -Xmx1g" \
        "${ES_IMAGE}" >/dev/null
    STARTED_ES_HERE=1
fi

echo "demo_compare_es: waiting for ES /_cluster/health"
es_ready=0
for _ in $(seq 1 120); do
    if curl -fsS -o /dev/null "http://127.0.0.1:${ES_PORT}/_cluster/health" 2>/dev/null; then
        es_ready=1
        break
    fi
    sleep 1
done
if (( ! es_ready )); then
    echo "demo_compare_es: ES never became ready on port ${ES_PORT}" >&2
    exit 1
fi

# --- spp_serve ---
echo "demo_compare_es: starting spp_serve on port ${SPP_PORT}"
"${SERVE}" --index "${INDEX_DIR}" --host 127.0.0.1 --port "${SPP_PORT}" \
    >"${SERVER_LOG}" 2>&1 &
SERVER_PID=$!
spp_ready=0
for _ in $(seq 1 50); do
    if curl -fsS -o /dev/null "http://127.0.0.1:${SPP_PORT}/_health" 2>/dev/null; then
        spp_ready=1
        break
    fi
    sleep 0.1
done
if (( ! spp_ready )); then
    echo "demo_compare_es: spp_serve never became ready" >&2
    cat "${SERVER_LOG}" >&2
    exit 1
fi

# --- run comparison ---
python3 scripts/compare_es.py \
    --spp-url "http://127.0.0.1:${SPP_PORT}" \
    --es-url "http://127.0.0.1:${ES_PORT}" \
    --duration "${DURATION}" \
    --concurrency "${CONCURRENCY}"
