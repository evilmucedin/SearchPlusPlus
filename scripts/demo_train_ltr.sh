#!/usr/bin/env bash
# demo_train_ltr.sh — end-to-end LTR training smoke test with checked-in data.
#
# Builds a tiny demo index (30 docs across three themes), then runs the full
# CatBoost pipeline: spp_export_features → catboost.fit → save_model('cpp').
#
# Intent: this is a *pipeline* smoke test. The 30-document corpus is too small
# for the resulting model to outrank BM25 on real workloads — its only job is
# to prove the wiring is alive end-to-end before you point train_ltr.py at
# your own judgments.
#
# Prereqs:
#   - cmake --build --preset default --target spp_index spp_export_features -j
#   - pip install catboost

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

INDEX_BIN=""
for c in \
    "build/release/tools/spp_index/spp_index" \
    "build/default/tools/spp_index/spp_index"; do
    if [[ -x "${c}" ]]; then
        INDEX_BIN="${c}"
        break
    fi
done
if [[ -z "${INDEX_BIN}" ]]; then
    echo "demo_train_ltr: spp_index not built." >&2
    echo "  cmake --preset default" >&2
    echo "  cmake --build --preset default --target spp_index spp_export_features -j" >&2
    exit 1
fi

TMP="$(mktemp -d -t spp_demo_ltr.XXXXXX)"
INDEX_DIR="${TMP}/index"
OUT_CPP="${1:-${REPO_ROOT}/build/demo_catboost_model.cpp}"
trap 'rm -rf "${TMP}"' EXIT

echo "demo_train_ltr: index → ${INDEX_DIR}"
mkdir -p "${INDEX_DIR}"
"${INDEX_BIN}" --index "${INDEX_DIR}" --mapping data/demo_mapping.json \
    < data/demo_corpus.jsonl

echo "demo_train_ltr: training (iterations=20, depth=3 — capped for the demo)"
python3 scripts/train_ltr.py \
    --index "${INDEX_DIR}" \
    --judgments data/demo_judgments.jsonl \
    --out "${OUT_CPP}" \
    --iterations 20 --depth 3

if [[ ! -s "${OUT_CPP}" ]]; then
    echo "demo_train_ltr: expected non-empty ${OUT_CPP}" >&2
    exit 1
fi

bytes=$(wc -c < "${OUT_CPP}")
echo "demo_train_ltr: ${OUT_CPP} (${bytes} bytes) — pipeline OK."
echo "  (this model is a smoke test, not production weights. Train on real judgments before deploying.)"
