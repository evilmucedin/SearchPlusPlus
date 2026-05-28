#!/usr/bin/env python3
"""Compare SearchPlusPlus and ElasticSearch on the same corpus + judgments.

Two measurements, side by side:

  1. Quality  — NDCG@10, P@5, MRR computed from a graded judgments JSONL.
                Same queries hit both engines; per-doc labels come from the
                judgments file (missing hits get label 0).
  2. Throughput — sustained req/s and peak 1-sec rate, plus latency p50/p95/p99,
                  measured by driving concurrent HTTP load against both engines
                  for `--duration` seconds.

The script assumes both engines are already running and reachable. It
re-creates the target indices on each engine, ingests the corpus, refreshes,
and then runs the two passes.

Inputs (all default to the checked-in demo files):
    --spp-url        http://127.0.0.1:9211     spp_serve base URL
    --es-url         http://127.0.0.1:9200     ElasticSearch base URL
    --index-name     bench_compare             index name (used on both engines)
    --corpus         data/demo_corpus.jsonl
    --mapping        data/demo_mapping.json
    --judgments      data/demo_judgments.jsonl
    --duration       10                        seconds of load per engine
    --concurrency    8                         concurrent in-flight reqs

The demo corpus is 30 documents — absolute numbers are too noisy to be a
benchmark. The point is the methodology: drop in your own corpus +
judgments and the comparison stays valid.
"""

import argparse
import base64
import json
import math
import statistics
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor


def _http(method, url, body=None, timeout=30.0, headers=None):
    """Tiny urllib wrapper. Returns (status, parsed_json_or_bytes)."""
    data = None
    if body is not None:
        if isinstance(body, (dict, list)):
            data = json.dumps(body).encode("utf-8")
        elif isinstance(body, str):
            data = body.encode("utf-8")
        else:
            data = body
    h = {"Content-Type": "application/json"}
    if headers:
        h.update(headers)
    req = urllib.request.Request(url, data=data, method=method, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
            code = resp.getcode()
    except urllib.error.HTTPError as e:
        raw = e.read()
        code = e.code
    if not raw:
        return code, None
    try:
        return code, json.loads(raw)
    except json.JSONDecodeError:
        return code, raw


def _load_corpus(path):
    docs = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            doc = json.loads(line)
            if "id" in doc and "_id" not in doc:
                doc["_id"] = doc.pop("id")
            if "_id" not in doc:
                raise SystemExit(f"compare_es: corpus row missing _id: {line[:120]}")
            docs.append(doc)
    return docs


def _load_judgments(path):
    by_query = defaultdict(dict)  # query -> {doc_id: label}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            by_query[row["query"]][row["doc_id"]] = int(row["label"])
    return dict(by_query)


# ---------- engine adapters ----------

class SppEngine:
    name = "SearchPlusPlus"

    def __init__(self, base_url, index_name, mapping):
        self.base = base_url.rstrip("/")
        self.index = index_name
        self.mapping = mapping

    def ping(self):
        code, _ = _http("GET", f"{self.base}/_health")
        return code == 200

    def reset_index(self):
        # PUT replaces an existing index in spp_serve.
        code, body = _http("PUT", f"{self.base}/{self.index}", self.mapping)
        if code >= 400:
            raise SystemExit(f"spp PUT index failed: {code} {body}")

    def index_docs(self, docs):
        for doc in docs:
            code, body = _http("POST", f"{self.base}/{self.index}/_doc", doc)
            if code >= 400:
                raise SystemExit(f"spp POST _doc failed: {code} {body}")
        _http("POST", f"{self.base}/{self.index}/_refresh")

    def search_url(self, q, default_field, size):
        params = {"q": q, "size": str(size)}
        if default_field:
            params["default_field"] = default_field
        return f"{self.base}/{self.index}/_search?{urllib.parse.urlencode(params)}"

    def search(self, q, default_field, size):
        code, body = _http("GET", self.search_url(q, default_field, size))
        if code >= 400 or not isinstance(body, dict):
            return []
        return [hit["_id"] for hit in body.get("hits", [])]


class EsEngine:
    name = "ElasticSearch"

    def __init__(self, base_url, index_name, mapping, basic_auth=None):
        self.base = base_url.rstrip("/")
        self.index = index_name
        self.mapping = mapping
        self.headers = {}
        if basic_auth:
            tok = base64.b64encode(basic_auth.encode("utf-8")).decode("ascii")
            self.headers["Authorization"] = f"Basic {tok}"

    def ping(self):
        code, _ = _http("GET", f"{self.base}/_cluster/health", headers=self.headers)
        return code == 200

    def reset_index(self):
        # DELETE if it already exists, then PUT with our schema.
        _http("DELETE", f"{self.base}/{self.index}", headers=self.headers)
        body = {"mappings": {"properties": {}}}
        for fname, fmap in self.mapping.get("mappings", {}).items():
            body["mappings"]["properties"][fname] = {"type": fmap.get("type", "text")}
        code, resp = _http("PUT", f"{self.base}/{self.index}", body, headers=self.headers)
        if code >= 400:
            raise SystemExit(f"es PUT index failed: {code} {resp}")

    def index_docs(self, docs):
        # Use bulk for speed — single request, NDJSON body.
        lines = []
        for d in docs:
            doc_id = d["_id"]
            body = {k: v for k, v in d.items() if k != "_id"}
            lines.append(json.dumps({"index": {"_index": self.index, "_id": doc_id}}))
            lines.append(json.dumps(body))
        payload = "\n".join(lines) + "\n"
        code, resp = _http("POST", f"{self.base}/_bulk",
                           body=payload, headers={**self.headers,
                                                  "Content-Type": "application/x-ndjson"})
        if code >= 400 or (isinstance(resp, dict) and resp.get("errors")):
            raise SystemExit(f"es _bulk failed: {code} {resp}")
        _http("POST", f"{self.base}/{self.index}/_refresh", headers=self.headers)

    def search_url(self, q, default_field, size):
        # Plain URL for the load test — body sent as POST per request.
        return f"{self.base}/{self.index}/_search"

    def _query_body(self, q, default_field, size):
        body = {"size": size,
                "query": {"query_string": {"query": q}},
                "_source": False}
        if default_field:
            body["query"]["query_string"]["default_field"] = default_field
        return body

    def search(self, q, default_field, size):
        body = self._query_body(q, default_field, size)
        code, resp = _http("POST", f"{self.base}/{self.index}/_search",
                           body, headers=self.headers)
        if code >= 400 or not isinstance(resp, dict):
            return []
        return [hit["_id"] for hit in resp.get("hits", {}).get("hits", [])]


# ---------- quality metrics ----------

def _dcg(labels):
    s = 0.0
    for i, lab in enumerate(labels, start=1):
        s += (2 ** lab - 1) / math.log2(i + 1)
    return s


def _ndcg_at_k(ranked_labels, ideal_labels, k):
    dcg = _dcg(ranked_labels[:k])
    ideal = _dcg(sorted(ideal_labels, reverse=True)[:k])
    return dcg / ideal if ideal > 0 else 0.0


def _precision_at_k(ranked_labels, k):
    if k == 0:
        return 0.0
    return sum(1 for lab in ranked_labels[:k] if lab > 0) / k


def _mrr(ranked_labels):
    for i, lab in enumerate(ranked_labels, start=1):
        if lab > 0:
            return 1.0 / i
    return 0.0


def _parse_query(q):
    """Extract default_field if the query uses `field:term` only."""
    # spp_serve's q= accepts plain text + ES-style `field:term`. We pass the
    # raw query through unchanged; default_field gives field for bare terms.
    if ":" in q and " " not in q:
        return q, q.split(":", 1)[0]
    return q, ""


def run_quality(engine, judgments, size=10):
    """Return dict of per-engine quality numbers averaged over queries."""
    ndcg, p5, mrr = [], [], []
    for query, labels in judgments.items():
        q_text, def_field = _parse_query(query)
        hits = engine.search(q_text, def_field, size)
        ranked = [labels.get(doc_id, 0) for doc_id in hits]
        ideal = list(labels.values())
        ndcg.append(_ndcg_at_k(ranked, ideal, 10))
        p5.append(_precision_at_k(ranked, 5))
        mrr.append(_mrr(ranked))
    n = max(1, len(judgments))
    return {
        "queries": len(judgments),
        "ndcg@10": sum(ndcg) / n,
        "p@5": sum(p5) / n,
        "mrr": sum(mrr) / n,
    }


# ---------- throughput ----------

def run_throughput(engine, judgments, duration, concurrency, warmup, size=10):
    """Sustained load against `engine`. Returns rps + latency stats."""
    # Round-robin through the judged queries.
    queries = [_parse_query(q) for q in judgments.keys()]
    if not queries:
        raise SystemExit("compare_es: no queries to drive load with")

    def one(idx):
        q, df = queries[idx % len(queries)]
        t0 = time.monotonic()
        try:
            engine.search(q, df, size)
            ok = True
        except Exception:
            ok = False
        return ok, time.monotonic() - t0

    # warmup
    if warmup > 0:
        end_w = time.monotonic() + warmup
        with ThreadPoolExecutor(max_workers=concurrency) as ex:
            futs = []
            i = 0
            while time.monotonic() < end_w:
                if len(futs) < concurrency:
                    futs.append(ex.submit(one, i))
                    i += 1
                else:
                    futs.pop(0).result()
            for f in futs:
                f.result()

    latencies = []
    buckets = []
    errors = 0
    lock = threading.Lock()
    start = time.monotonic()
    end = start + duration

    def worker(seed):
        nonlocal errors
        i = seed
        while time.monotonic() < end:
            ok, dt = one(i)
            i += 1
            t_finish = time.monotonic()
            bidx = int(t_finish - start)
            with lock:
                latencies.append(dt)
                while bidx >= len(buckets):
                    buckets.append(0)
                buckets[bidx] += 1
                if not ok:
                    errors += 1

    with ThreadPoolExecutor(max_workers=concurrency) as ex:
        futs = [ex.submit(worker, w) for w in range(concurrency)]
        for f in futs:
            f.result()

    elapsed = time.monotonic() - start
    total = len(latencies)
    rps = total / elapsed if elapsed > 0 else 0.0
    # Drop edge buckets to avoid spin-up/spin-down skew.
    inner = buckets[1:-1] if len(buckets) >= 3 else buckets
    peak_rps = max(inner) if inner else 0

    latencies.sort()

    def pct(p):
        if not latencies:
            return 0.0
        k = max(0, min(len(latencies) - 1, int(len(latencies) * p)))
        return latencies[k] * 1000

    return {
        "elapsed_s": elapsed,
        "requests": total,
        "errors": errors,
        "rps_mean": rps,
        "rpm_mean": rps * 60.0,
        "peak_1s_rps": peak_rps,
        "peak_rpm": peak_rps * 60,
        "lat_p50_ms": pct(0.50),
        "lat_p95_ms": pct(0.95),
        "lat_p99_ms": pct(0.99),
        "lat_max_ms": (latencies[-1] * 1000) if latencies else 0.0,
        "lat_mean_ms": (statistics.fmean(latencies) * 1000) if latencies else 0.0,
    }


# ---------- report ----------

def _fmt(v):
    if isinstance(v, float):
        return f"{v:>12.3f}"
    return f"{v:>12}"


def report(spp_q, es_q, spp_t, es_t):
    print()
    print("=" * 64)
    print("  SearchPlusPlus  vs  ElasticSearch  —  side-by-side")
    print("=" * 64)
    print()
    print("Search quality (higher is better)")
    print(f"  {'metric':<14} {'spp':>12} {'es':>12} {'delta':>12}")
    for key in ("queries", "ndcg@10", "p@5", "mrr"):
        a, b = spp_q[key], es_q[key]
        delta = (a - b) if isinstance(a, float) else (a - b)
        print(f"  {key:<14} {_fmt(a)} {_fmt(b)} {_fmt(delta)}")
    print()
    print("Search performance (req/min higher better, latency lower better)")
    perf_keys = [
        ("requests",     "n"),
        ("errors",       "n"),
        ("rps_mean",     "req/s"),
        ("rpm_mean",     "req/min"),
        ("peak_rpm",     "req/min*"),
        ("lat_mean_ms",  "ms"),
        ("lat_p50_ms",   "ms"),
        ("lat_p95_ms",   "ms"),
        ("lat_p99_ms",   "ms"),
        ("lat_max_ms",   "ms"),
    ]
    print(f"  {'metric':<14} {'spp':>12} {'es':>12} {'unit':>10}")
    for key, unit in perf_keys:
        a, b = spp_t[key], es_t[key]
        print(f"  {key:<14} {_fmt(a)} {_fmt(b)} {unit:>10}")
    print()
    print("  * peak_rpm = 60 * max 1-sec window in the measured run")
    print()
    # Headline summary.
    winner_q = "spp" if spp_q["ndcg@10"] >= es_q["ndcg@10"] else "es"
    winner_p = "spp" if spp_t["rpm_mean"] >= es_t["rpm_mean"] else "es"
    print(f"Quality winner (NDCG@10):     {winner_q}")
    print(f"Throughput winner (req/min):  {winner_p}")
    print()


def main():
    ap = argparse.ArgumentParser(
        description="Compare SearchPlusPlus and ElasticSearch quality + throughput.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--spp-url", default="http://127.0.0.1:9211")
    ap.add_argument("--es-url", default="http://127.0.0.1:9200")
    ap.add_argument("--es-auth", default=None,
                    help="user:password for ES basic auth (only if security on)")
    ap.add_argument("--index-name", default=None,
                    help="index name shared across both engines "
                         "(default: random, so each run starts clean)")
    ap.add_argument("--corpus", default="data/demo_corpus.jsonl")
    ap.add_argument("--mapping", default="data/demo_mapping.json")
    ap.add_argument("--judgments", default="data/demo_judgments.jsonl")
    ap.add_argument("--duration", type=float, default=10.0,
                    help="seconds of load per engine")
    ap.add_argument("--concurrency", type=int, default=8)
    ap.add_argument("--warmup", type=float, default=1.0)
    ap.add_argument("--size", type=int, default=10)
    ap.add_argument("--json", action="store_true",
                    help="dump full result dict instead of the human table")
    args = ap.parse_args()

    with open(args.mapping, "r", encoding="utf-8") as f:
        mapping = json.load(f)
    docs = _load_corpus(args.corpus)
    judgments = _load_judgments(args.judgments)
    # spp_serve has no DELETE /<index>, so we pick a unique name per run to
    # avoid 409s when the previous run's index is still around.
    index_name = args.index_name or f"compare_{uuid.uuid4().hex[:8]}"
    print(f"compare_es: {len(docs)} docs, {len(judgments)} unique queries  "
          f"(index={index_name})")

    spp = SppEngine(args.spp_url, index_name, mapping)
    es = EsEngine(args.es_url, index_name, mapping, basic_auth=args.es_auth)

    for engine in (spp, es):
        if not engine.ping():
            raise SystemExit(
                f"compare_es: {engine.name} not reachable at "
                f"{engine.base}. Start it first."
            )

    print(f"compare_es: indexing into spp_serve ({args.spp_url})")
    spp.reset_index()
    spp.index_docs(docs)

    print(f"compare_es: indexing into ElasticSearch ({args.es_url})")
    es.reset_index()
    es.index_docs(docs)

    print(f"compare_es: running quality pass over {len(judgments)} queries")
    spp_q = run_quality(spp, judgments, size=args.size)
    es_q = run_quality(es, judgments, size=args.size)

    print(f"compare_es: running throughput pass "
          f"({args.duration:.0f}s @ concurrency={args.concurrency})")
    spp_t = run_throughput(spp, judgments, args.duration,
                           args.concurrency, args.warmup, size=args.size)
    es_t = run_throughput(es, judgments, args.duration,
                          args.concurrency, args.warmup, size=args.size)

    if args.json:
        print(json.dumps({
            "spp": {"quality": spp_q, "perf": spp_t},
            "es":  {"quality": es_q,  "perf": es_t},
        }, indent=2))
    else:
        report(spp_q, es_q, spp_t, es_t)


if __name__ == "__main__":
    main()
