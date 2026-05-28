#!/usr/bin/env python3
"""Search-throughput benchmark for a running spp_serve instance.

Drives the HTTP search route under sustained concurrent load for a fixed
duration. The headline number is "max searches per minute" — both the
projected sustained rate and the peak observed in any one-second window.

Usage:
    scripts/bench_search.py --url http://127.0.0.1:9200 --index wiki \\
        [--queries queries.txt] [--duration 60] [--concurrency 16] \\
        [--warmup 5] [--size 10]

queries.txt is one query per line. Comment lines (#...) and blanks ignored.
Each line may optionally specify a default field as `field<TAB>query`.

Designed to pair with the new max_concurrent_searches cap (PR #13): if you
push concurrency past 2*CPU and watch the cap kick in, the histogram
flattens but request count keeps climbing — searches queue instead of error.
"""

import argparse
import json
import statistics
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

_DEFAULT_QUERIES = [
    ("", "search engine"),
    ("", "inverted index"),
    ("", "bm25"),
    ("", "tokenizer"),
    ("body", "posting list"),
    ("title", "document"),
]


def _load_queries(path):
    out = []
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "\t" in line:
                field, q = line.split("\t", 1)
                out.append((field.strip(), q.strip()))
            else:
                out.append(("", line))
    if not out:
        raise SystemExit(f"bench_search: no queries in {path}")
    return out


def _build_url(base, index, field, q, size):
    params = {"q": q, "size": str(size)}
    if field:
        params["default_field"] = field
    qs = urllib.parse.urlencode(params)
    return f"{base.rstrip('/')}/{index.strip('/')}/_search?{qs}"


def _one_request(url, timeout):
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            # Read the body so we count full request handling, not just headers.
            resp.read()
            code = resp.getcode()
            ok = 200 <= code < 300
    except urllib.error.HTTPError as e:
        code = e.code
        ok = False
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        code = type(e).__name__
        ok = False
    return ok, code, time.monotonic() - t0


def _percentile(sorted_xs, p):
    if not sorted_xs:
        return 0.0
    k = max(0, min(len(sorted_xs) - 1, int(len(sorted_xs) * p)))
    return sorted_xs[k]


def main():
    ap = argparse.ArgumentParser(
        description="Benchmark spp_serve search throughput and latency.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--url", default="http://127.0.0.1:9200",
                    help="base URL of the running spp_serve")
    ap.add_argument("--index", required=True,
                    help="index name (path segment in the search URL)")
    ap.add_argument("--queries", default=None,
                    help="file with one query per line (optionally 'field<TAB>q'); "
                         "defaults to a small built-in list")
    ap.add_argument("--duration", type=float, default=60.0,
                    help="seconds of load (default 60)")
    ap.add_argument("--concurrency", type=int, default=16,
                    help="concurrent in-flight requests (default 16)")
    ap.add_argument("--warmup", type=float, default=2.0,
                    help="warmup seconds before measurement (default 2)")
    ap.add_argument("--size", type=int, default=10,
                    help="hits per response (default 10)")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="per-request timeout in seconds (default 30)")
    ap.add_argument("--json", action="store_true",
                    help="emit machine-readable summary as a single JSON object")
    args = ap.parse_args()

    if args.concurrency < 1:
        raise SystemExit("--concurrency must be >= 1")
    if args.duration <= 0:
        raise SystemExit("--duration must be > 0")

    queries = _load_queries(args.queries) if args.queries else _DEFAULT_QUERIES
    urls = [_build_url(args.url, args.index, f, q, args.size) for f, q in queries]

    # Sanity probe — fail fast with a real error rather than a wall of stack traces.
    probe_ok, probe_code, _ = _one_request(urls[0], args.timeout)
    if not probe_ok:
        raise SystemExit(
            f"bench_search: probe request to {urls[0]} failed (code={probe_code}). "
            "Is spp_serve running and the index populated?"
        )

    # Warmup — exercises caches without polluting measurements.
    if args.warmup > 0:
        warm_end = time.monotonic() + args.warmup
        with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
            futures = []
            i = 0
            while time.monotonic() < warm_end:
                if len(futures) < args.concurrency:
                    futures.append(ex.submit(_one_request, urls[i % len(urls)], args.timeout))
                    i += 1
                else:
                    futures[0].result()
                    futures.pop(0)
            for f in futures:
                f.result()

    # Measured run.
    latencies = []
    errors = Counter()
    buckets = []
    lock = threading.Lock()
    start = time.monotonic()
    end = start + args.duration

    def worker(seed):
        i = seed
        while True:
            now = time.monotonic()
            if now >= end:
                return
            ok, code, dt = _one_request(urls[i % len(urls)], args.timeout)
            i += 1
            t_finish = time.monotonic()
            bucket_idx = int(t_finish - start)
            with lock:
                latencies.append(dt)
                while bucket_idx >= len(buckets):
                    buckets.append(0)
                buckets[bucket_idx] += 1
                if not ok:
                    errors[str(code)] += 1

    with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        futures = [ex.submit(worker, w) for w in range(args.concurrency)]
        for f in futures:
            f.result()

    elapsed = time.monotonic() - start
    total = len(latencies)
    err_count = sum(errors.values())
    ok_count = total - err_count
    rps = ok_count / elapsed if elapsed > 0 else 0.0
    rpm = rps * 60.0

    # Peak 1-sec window — projected to per-minute for the headline metric.
    # Drop the first and last buckets when we have enough data: the first is
    # contaminated by spin-up, the last is almost always a partial second.
    inner = buckets[1:-1] if len(buckets) >= 3 else buckets
    peak_rps = max(inner) if inner else 0
    peak_rpm = peak_rps * 60

    # Rolling 60s window — only meaningful if duration > 60s.
    rolling_60s_rpm = 0
    if len(buckets) >= 60:
        cur = sum(buckets[:60])
        best = cur
        for i in range(60, len(buckets)):
            cur += buckets[i] - buckets[i - 60]
            if cur > best:
                best = cur
        rolling_60s_rpm = best

    latencies.sort()
    p50 = _percentile(latencies, 0.50) * 1000
    p95 = _percentile(latencies, 0.95) * 1000
    p99 = _percentile(latencies, 0.99) * 1000
    p_max = (latencies[-1] * 1000) if latencies else 0.0
    p_mean = (statistics.fmean(latencies) * 1000) if latencies else 0.0

    if args.json:
        out = {
            "url": args.url,
            "index": args.index,
            "concurrency": args.concurrency,
            "duration_s": elapsed,
            "warmup_s": args.warmup,
            "requests_total": total,
            "requests_ok": ok_count,
            "requests_err": err_count,
            "errors": dict(errors),
            "throughput_rps_mean": rps,
            "throughput_rpm_mean": rpm,
            "peak_1s_rps": peak_rps,
            "max_searches_per_minute_peak": peak_rpm,
            "max_searches_per_minute_rolling60s": rolling_60s_rpm,
            "latency_ms": {
                "mean": p_mean, "p50": p50, "p95": p95, "p99": p99, "max": p_max,
            },
        }
        print(json.dumps(out, indent=2))
        return

    print("=== spp_serve search benchmark ===")
    print(f"url:                 {args.url}")
    print(f"index:               {args.index}")
    print(f"concurrency:         {args.concurrency}")
    print(f"duration:            {elapsed:.2f}s  (warmup {args.warmup:.1f}s)")
    print(f"distinct queries:    {len(queries)}")
    print()
    print(f"requests total:      {total}")
    print(f"requests ok:         {ok_count}")
    if err_count:
        print(f"requests error:      {err_count}  {dict(errors)}")
    print()
    print(f"throughput (mean):   {rps:8.1f} req/s   {rpm:10.0f} req/min")
    print(f"peak 1-sec window:   {peak_rps:8d} req/s   {peak_rpm:10d} req/min  <-- max searches/min")
    if rolling_60s_rpm:
        print(f"best 60-sec window:  {rolling_60s_rpm:>30d} req/min")
    print()
    print("latency (ms)")
    print(f"  mean: {p_mean:8.2f}   p50: {p50:8.2f}   p95: {p95:8.2f}"
          f"   p99: {p99:8.2f}   max: {p_max:8.2f}")

    if err_count and err_count == total:
        sys.exit(2)


if __name__ == "__main__":
    main()
