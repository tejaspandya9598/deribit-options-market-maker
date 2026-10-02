"""Check recorded books against REST snapshots.

  sample   While the recorder runs, fetch full-depth REST books (public/get_order_book) for random
           recorded instruments and save them to runs/checks/<stamp>.jsonl.
  verify   Rebuild books from data/raw/ (snapshot + changes) and compare full ladders at the
           message whose change_id equals each REST snapshot's change_id.
  tob      Top of book after every book message, in the same CSV format and with the same
           rebuild rules as `mm_engine --tob-out`, for a byte-for-byte comparison.

The 100ms book channel batches changes, so a REST change_id can fall inside a batch and never
appear in the stream. A sample is "inside_batch" only when all of these hold:
  - two consecutive stream messages with an unbroken prev_change_id chain bracket its change_id,
  - at most MAX_UNTOUCHED levels differ from the stream outside the levels that batch touched
    (an order placed and pulled within one batch is absent from the batch message).
Anything else that can't be compared (gap, no snapshot, missing from the stream, or a bracket that
fails the check above) is "unexplained" and counts as a failure.
REST `timestamp` is reported but not used: its meaning beyond "epoch ms" is undocumented, and on
exact matches it differs from the stream timestamp of the same change_id by -0.8 s to +199 s
(median +2.9 s, run of 2026-09-27), so it is not the book's state time.
Pass: match / (samples - inside_batch) >= 0.99, and inside_batch <= 10% of samples.

Run:
  uv run --env-file .env python recorder/check_book.py sample --count 200 --every 20
  uv run --env-file .env python recorder/check_book.py verify runs/checks/<stamp>.jsonl
  uv run --env-file .env python recorder/check_book.py tob data/raw/DAY/21.jsonl.gz ... --out F
"""

from __future__ import annotations

import argparse
import bisect
import gzip
import json
import math
import os
import random
import sys
import time
import urllib.parse
import urllib.request
import zlib
from datetime import UTC, datetime
from pathlib import Path

DATA_DIR = Path(os.environ.get("DATA_DIR", "data"))
RUNS_DIR = Path(os.environ.get("RUNS_DIR", "runs"))
REST_URL = os.environ.get("DERIBIT_REST_URL", "https://www.deribit.com/api/v2")
TARGET = 0.99
MAX_INSIDE_BATCH = 0.10  # more than this and too few samples are checkable
MAX_UNTOUCHED = 1  # transient levels allowed outside a batch's touched levels


def utc(ns: int) -> datetime:
    return datetime.fromtimestamp(ns / 1e9, UTC)


# ---------- sample ----------


def recorded_instruments() -> list[str]:
    """Instrument list from the recorder's latest 'universe' log event."""
    logs = sorted((DATA_DIR / "logs").glob("recorder-*.jsonl"))
    for path in reversed(logs):
        latest = None
        with open(path) as fh:
            for line in fh:
                if '"event": "universe"' in line:
                    latest = line
        if latest:
            return json.loads(latest)["instruments"]
    sys.exit(f"no universe event in {DATA_DIR / 'logs'}; is the recorder running?")


def get_order_book(name: str) -> dict:
    q = urllib.parse.urlencode({"instrument_name": name, "depth": 10000})
    with urllib.request.urlopen(f"{REST_URL}/public/get_order_book?{q}", timeout=10) as r:
        return json.load(r)["result"]


def cmd_sample(args: argparse.Namespace) -> None:
    names = recorded_instruments()
    out_dir = RUNS_DIR / "checks"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{utc(time.time_ns()).strftime('%Y%m%dT%H%M%SZ')}.jsonl"
    print(f"sampling {args.count} books every {args.every}s from {len(names)} instruments -> {out}")
    with open(out, "w", buffering=1) as fh:
        for k in range(args.count):
            name = random.choice(names)
            local_ns = time.time_ns()
            try:
                book = get_order_book(name)
            except OSError as e:
                print(f"{k}: {name} fetch failed: {e}", file=sys.stderr)
            else:
                rec = {"local_ns": local_ns, "instrument": name, "result": book}
                fh.write(json.dumps(rec) + "\n")
            if k + 1 < args.count:
                time.sleep(args.every)
    print(f"done: {out}")


# ---------- verify ----------


def raw_files(end_ns: int, since: str | None) -> list[Path]:
    """Raw files from `since` (YYYY-MM-DD/HH) or the first hour, up to end_ns's hour + 1."""
    files = sorted((DATA_DIR / "raw").glob("*/*.jsonl.gz"))
    last = utc(end_ns).strftime("%Y-%m-%d/%H")
    keep = []
    for f in files:
        key = f"{f.parent.name}/{f.name[:2]}"
        if since and key < since:
            continue
        if key > last:
            # one more hour covers a snapshot change_id recorded just after the hour turned
            keep.append(f)
            break
        keep.append(f)
    return keep


def read_lines(path: Path):
    """Lines of a gzip (or plain, e.g. a book checkpoint) file, stopping cleanly at the
    unfinished tail of the live hour."""
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt") as fh:
        try:
            for line in fh:
                if line.endswith("\n"):
                    yield line
        except EOFError, gzip.BadGzipFile, zlib.error:
            return


def ladder(levels: list) -> dict[float, float]:
    return {float(p): float(a) for p, a in levels}


def first_diff(side: str, mine: dict, rest: dict) -> dict | None:
    for price in sorted(set(mine) | set(rest), reverse=(side == "bids")):
        if mine.get(price) != rest.get(price):
            return {
                "side": side,
                "price": price,
                "stream": mine.get(price),
                "rest": rest.get(price),
            }
    return None


def classify_bracketed(
    rest: dict,
    before: tuple[dict, dict],
    touched: set[tuple[int, float]],
    before_ts: int | None,
    after_ts: int | None,
) -> dict:
    """Is a REST book plausibly a state inside the batch between `before` and the next message?
    Levels the batch didn't touch are the same before and after it, so REST must agree there.
    Timestamps are reported only (see module docstring)."""
    rest_ladders = (ladder(rest["bids"]), ladder(rest["asks"]))
    untouched = 0
    for k in (0, 1):
        for price in set(rest_ladders[k]) | set(before[k]):
            if (k, price) not in touched and rest_ladders[k].get(price) != before[k].get(price):
                untouched += 1
    detail = {
        "stream_before_ts": before_ts,
        "rest_ts": rest.get("timestamp"),
        "stream_after_ts": after_ts,
        "untouched_level_diffs": untouched,
    }
    if untouched <= MAX_UNTOUCHED:
        return {"status": "inside_batch", **detail}
    return {"status": "unexplained", "reason": "levels differ outside batch", **detail}


def cmd_verify(args: argparse.Namespace) -> int:
    samples = [json.loads(line) for line in open(args.samples) if line.strip()]
    if not samples:
        sys.exit("no samples")
    # instrument -> {change_id: [sample index, ...]}
    targets: dict[str, dict[int, list[int]]] = {}
    for i, s in enumerate(samples):
        targets.setdefault(s["instrument"], {}).setdefault(s["result"]["change_id"], []).append(i)

    books: dict[str, tuple[dict, dict]] = {}
    last_change: dict[str, int] = {}
    last_ts: dict[str, int] = {}
    outcome: list[dict | None] = [None] * len(samples)
    t0 = min(s["local_ns"] for s in samples)
    t1 = max(s["local_ns"] for s in samples)
    sorted_ids = {name: sorted(ids) for name, ids in targets.items()}
    files = raw_files(t1, args.since)
    n_msgs = 0
    for path in files:
        for line in read_lines(path):
            if '"channel":"book.' not in line:
                continue
            rec = json.loads(line)
            data = rec["msg"]["params"]["data"]
            name = data["instrument_name"]
            if name not in targets:
                continue
            n_msgs += 1
            cid = data["change_id"]
            chained = name in books and data.get("prev_change_id") == last_change.get(name)
            if data["type"] == "snapshot":
                books[name] = (ladder_entries({}, data["bids"]), ladder_entries({}, data["asks"]))
            elif chained:
                bids, asks = books[name]
                # REST states strictly between two chained messages were never published: the
                # 100ms channel sends only the state at the end of each batch.
                ids = sorted_ids[name]
                lo = bisect.bisect_right(ids, last_change[name])
                hi = bisect.bisect_left(ids, cid)
                bracketed = [i for t in ids[lo:hi] for i in targets[name][t] if outcome[i] is None]
                if bracketed:
                    before = (dict(bids), dict(asks))
                    touched = {(0, float(e[1])) for e in data["bids"]}
                    touched |= {(1, float(e[1])) for e in data["asks"]}
                    for i in bracketed:
                        outcome[i] = {
                            "stream_before": last_change[name],
                            "stream_after": cid,
                            **classify_bracketed(
                                samples[i]["result"],
                                before,
                                touched,
                                last_ts.get(name),
                                data.get("timestamp"),
                            ),
                        }
                ladder_entries(bids, data["bids"])
                ladder_entries(asks, data["asks"])
            else:
                books.pop(name, None)  # gap or no snapshot yet: wait for the next snapshot
            last_change[name] = cid
            last_ts[name] = data.get("timestamp")
            hits = targets[name].get(cid)
            if not hits or name not in books:
                continue
            bids, asks = books[name]
            for i in hits:
                rest = samples[i]["result"]
                diff = first_diff("bids", bids, ladder(rest["bids"])) or first_diff(
                    "asks", asks, ladder(rest["asks"])
                )
                outcome[i] = {"status": "mismatch", "diff": diff} if diff else {"status": "match"}

    counts = {"match": 0, "mismatch": 0, "inside_batch": 0, "unexplained": 0}
    details = []
    for i, s in enumerate(samples):
        o = outcome[i] or {"status": "unexplained"}
        counts[o["status"]] += 1
        details.append({"instrument": s["instrument"], "change_id": s["result"]["change_id"], **o})
    n = len(samples)
    checkable = n - counts["inside_batch"]  # mismatches and unexplained both count against us
    rate = counts["match"] / checkable if checkable else float("nan")
    batch_share = counts["inside_batch"] / n
    passed = checkable > 0 and rate >= TARGET and batch_share <= MAX_INSIDE_BATCH
    summary = {
        "samples": n,
        **counts,
        "match_rate": rate,
        "match_share_all_samples": counts["match"] / n,
        "inside_batch_share": batch_share,
        "target": TARGET,
        "max_inside_batch_share": MAX_INSIDE_BATCH,
        "passed": passed,
        "raw_files": [str(f) for f in files],
        "book_messages_read": n_msgs,
        "sample_window_utc": [utc(t0).isoformat(), utc(t1).isoformat()],
    }
    out = Path(args.samples).with_suffix(".verify.json")
    out.write_text(json.dumps({"summary": summary, "details": details}, indent=1))

    print(
        f"samples {n}: match {counts['match']}, mismatch {counts['mismatch']}, "
        f"inside_batch {counts['inside_batch']}, unexplained {counts['unexplained']}"
    )
    print(
        f"match rate {rate:.4f} = match / (samples - inside_batch), target {TARGET}; "
        f"match share of all samples {counts['match'] / n:.4f}; "
        f"inside_batch share {batch_share:.4f} (max {MAX_INSIDE_BATCH})"
    )
    for d in details:
        if d["status"] == "mismatch":
            print(f"  mismatch {d['instrument']} change_id={d['change_id']}: {d['diff']}")
        elif d["status"] == "unexplained":
            print(
                f"  unexplained {d['instrument']} change_id={d['change_id']}: "
                f"{d.get('reason', 'not in stream / gap / no snapshot')}"
            )
    print(f"{'PASS' if passed else 'FAIL'}; details -> {out}")
    return 0 if passed else 1


def ladder_entries(side: dict[float, float], entries: list) -> dict[float, float]:
    """Apply Deribit book entries [action, price, amount] (new / change / delete) in place."""
    for action, price, amount in entries:
        if action == "delete":
            side.pop(float(price), None)
        else:
            side[float(price)] = float(amount)
    return side


TOB_HEADER = "recv_ns,instrument,change_id,valid,bid_px_e8,bid_amt_e8,ask_px_e8,ask_amt_e8\n"


def to_e8(v: float) -> int:
    """Same expression as the engine's to_e8() (engine/src/types.hpp)."""
    return int(math.floor(v * 1e8 + 0.5))


def cmd_tob(args: argparse.Namespace) -> None:
    """Rules, as in engine/src/order_book.hpp: a snapshot resets the book and makes it valid; a
    change applies only if the book is valid and prev_change_id equals the last change_id;
    anything else clears the book and makes it invalid until the next snapshot. A recorder
    connect or disconnect event invalidates every book. The last change_id updates on every
    message."""
    books: dict[str, dict] = {}
    out = open(args.out, "w") if args.out else sys.stdout
    out.write(TOB_HEADER)
    for path in args.files:
        for line in read_lines(Path(path)):
            if '"channel":"book.' not in line and '"event":' not in line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "event" in rec:
                if rec["event"].get("type") in ("connect", "disconnect"):
                    for b in books.values():
                        b["valid"] = False
                        b["bids"].clear()
                        b["asks"].clear()
                continue
            params = rec["msg"].get("params", {})
            if not params.get("channel", "").startswith("book."):
                continue
            d = params["data"]
            name = d["instrument_name"]
            b = books.setdefault(name, {"valid": False, "cid": 0, "bids": {}, "asks": {}})
            snapshot = d.get("type") == "snapshot"
            if snapshot or (b["valid"] and d.get("prev_change_id", -1) == b["cid"]):
                if snapshot:
                    b["bids"].clear()
                    b["asks"].clear()
                    b["valid"] = True
                for side in ("bids", "asks"):
                    levels = b[side]
                    for action, price, amount in d[side]:
                        if action == "delete":
                            levels.pop(to_e8(price), None)
                        else:
                            levels[to_e8(price)] = amount
            else:
                b["valid"] = False
                b["bids"].clear()
                b["asks"].clear()
            b["cid"] = d["change_id"]
            bid = ask = ","
            if b["valid"] and b["bids"]:
                px = max(b["bids"])
                bid = f"{px},{to_e8(b['bids'][px])}"
            if b["valid"] and b["asks"]:
                px = min(b["asks"])
                ask = f"{px},{to_e8(b['asks'][px])}"
            valid = 1 if b["valid"] else 0
            out.write(f"{rec['recv_ns']},{name},{d['change_id']},{valid},{bid},{ask}\n")
    if out is not sys.stdout:
        out.close()


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sample", help="fetch REST snapshots while the recorder runs")
    s.add_argument("--count", type=int, default=200)
    s.add_argument("--every", type=float, default=20, help="seconds between samples")
    v = sub.add_parser("verify", help="compare rebuilt books with saved REST snapshots")
    v.add_argument("samples", help="runs/checks/<stamp>.jsonl from 'sample'")
    v.add_argument("--since", help="first raw hour to read, YYYY-MM-DD/HH (default: all)")
    t = sub.add_parser("tob", help="top of book after every book message (engine format)")
    t.add_argument("files", nargs="+", help="raw .jsonl.gz files (or checkpoint .jsonl), in order")
    t.add_argument("--out", help="output CSV (default: stdout)")
    args = p.parse_args()
    if args.cmd == "sample":
        cmd_sample(args)
    elif args.cmd == "tob":
        cmd_tob(args)
    else:
        sys.exit(cmd_verify(args))


if __name__ == "__main__":
    main()
