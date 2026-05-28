#!/usr/bin/env python3
"""train_ltr.py — train a SearchPlusPlus LTR model from a judgments file.

End-to-end CatBoost pipeline:

  judgments.jsonl  ──spp_export_features──▶  pool.tsv  ──CatBoost.fit──▶  model.bin
                                                                            │
                                                                            ▼ save_model(format='cpp')
                                                                       catboost_model.cpp

The output `.cpp` defines `apply_catboost_model(...)` and drops into
`models/catboost_model.cpp` in this repo. Rebuild and the embedded model
is live.

Positive (label > 0) and negative (label = 0) judgments are both first-class
inputs — CatBoost's ranking loss needs both to learn what *not* to surface.

Example:
  scripts/train_ltr.py \\
      --index /var/lib/spp/wiki \\
      --judgments judgments.jsonl \\
      --out models/catboost_model.cpp \\
      --iterations 300 --depth 6
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def find_export_features() -> Path:
    """Locate spp_export_features in the usual build trees or on PATH."""
    candidates = [
        REPO_ROOT / "build/release/tools/spp_export_features/spp_export_features",
        REPO_ROOT / "build/default/tools/spp_export_features/spp_export_features",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    on_path = shutil.which("spp_export_features")
    if on_path:
        return Path(on_path)
    sys.exit(
        "train_ltr.py: spp_export_features not found.\n"
        "  Build it first:\n"
        "    cmake --preset default\n"
        "    cmake --build --preset default --target spp_export_features -j"
    )


def import_catboost():
    try:
        import catboost  # noqa: F401
        return catboost
    except ImportError:
        sys.exit(
            "train_ltr.py: the `catboost` Python package is not installed.\n"
            "  pip install catboost"
        )


def run_export(export_bin: Path, index: Path, judgments: Path,
               pool_tsv: Path, top_n: int, default_field: str | None) -> None:
    cmd = [
        str(export_bin),
        "--index", str(index),
        "--judgments", str(judgments),
        "--out", str(pool_tsv),
        "--top-n", str(top_n),
    ]
    if default_field:
        cmd += ["--default-field", default_field]
    print(f"[1/3] running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)
    if pool_tsv.stat().st_size == 0:
        sys.exit("train_ltr.py: spp_export_features produced an empty pool — "
                 "are the judgments matched against the right index?")


def write_column_descriptor(cd_path: Path) -> None:
    # spp_export_features TSV layout:
    #   col 0 = label, col 1 = query_id (group), col 2..N = numeric features.
    # CatBoost auto-classifies unlisted columns as numeric, so we only need
    # to flag the label + group columns.
    cd_path.write_text("0\tLabel\n1\tGroupId\n")


def train(catboost_mod, pool_tsv: Path, cd_path: Path, loss: str,
          iterations: int, depth: int, model_bin: Path) -> None:
    print(f"[2/3] training CatBoost: loss={loss} iterations={iterations} depth={depth}")
    pool = catboost_mod.Pool(data=str(pool_tsv), column_description=str(cd_path))
    model = catboost_mod.CatBoost(
        params={
            "loss_function": loss,
            "eval_metric": "NDCG",
            "iterations": iterations,
            "depth": depth,
            "verbose": 50,
            "allow_writing_files": False,
        }
    )
    model.fit(pool)
    model.save_model(str(model_bin))
    return model


def export_cpp(model, out_cpp: Path) -> None:
    print(f"[3/3] exporting C++ to {out_cpp}")
    out_cpp.parent.mkdir(parents=True, exist_ok=True)
    model.save_model(str(out_cpp), format="cpp")
    if out_cpp.stat().st_size == 0:
        sys.exit(f"train_ltr.py: empty .cpp at {out_cpp}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--index", required=True, type=Path,
                   help="Directory of a built SearchPlusPlus index.")
    p.add_argument("--judgments", required=True, type=Path,
                   help="JSONL file: {\"query\":..., \"doc_id\":..., \"label\":int, "
                        "[\"query_id\":int]} per line. Mix positives (label>0) "
                        "and negatives (label=0).")
    p.add_argument("--out", default=str(REPO_ROOT / "models/catboost_model.cpp"),
                   type=Path,
                   help="Where to write the exported C++ model. "
                        "Default: models/catboost_model.cpp (the path the engine compiles in).")
    p.add_argument("--iterations", type=int, default=200,
                   help="Number of boosting iterations.")
    p.add_argument("--depth", type=int, default=6, help="Tree depth.")
    p.add_argument("--loss", default="YetiRank",
                   help="CatBoost loss function (YetiRank is the default pairwise ranker).")
    p.add_argument("--top-n", type=int, default=1000,
                   help="Candidate pool size used by spp_export_features.")
    p.add_argument("--default-field", default=None,
                   help="Override the implicit field for bare query terms.")
    args = p.parse_args()

    if not args.index.is_dir():
        sys.exit(f"train_ltr.py: --index is not a directory: {args.index}")
    if not args.judgments.is_file():
        sys.exit(f"train_ltr.py: --judgments file does not exist: {args.judgments}")

    export_bin = find_export_features()
    catboost_mod = import_catboost()

    with tempfile.TemporaryDirectory(prefix="spp_train_ltr_") as tmpdir:
        tmp = Path(tmpdir)
        pool_tsv = tmp / "pool.tsv"
        cd_path = tmp / "pool.cd"
        model_bin = tmp / "model.bin"

        run_export(export_bin, args.index, args.judgments, pool_tsv,
                   args.top_n, args.default_field)
        write_column_descriptor(cd_path)
        model = train(catboost_mod, pool_tsv, cd_path, args.loss,
                      args.iterations, args.depth, model_bin)
        export_cpp(model, args.out)

    print()
    print("Done. To deploy:")
    print(f"  cmake --build --preset default -j")
    print(f"  (the C++ binary embeds {args.out} as the CatBoost ranker.)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
