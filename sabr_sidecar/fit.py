"""Build the SABR fit cache for recorded days.

For each day, every `--every` seconds of exchange time, fit SABR (fixed beta) to each recorded
expiry with sabr.calibration.calibrate:
  - quotes: out-of-the-money options (calls K >= F, puts K < F) with both bid and ask IV
  - market vol: mid of bid/ask IV; weight: 1 / (ask IV - bid IV), spread floored at MIN_SPREAD
  - forward: median of the expiry's ticker underlying_price
  - warm start from the previous fit of the same expiry
  - only tickers no older than MAX_TICKER_AGE_S at fit time

No look-ahead: a fit at time t runs before the first ticker stamped after t is applied, so it
only uses data up to t. Its result is usable at t + compute_ns (measured calibrate() time).

Output: cache/sabr/beta-<beta>/YYYY-MM-DD.jsonl.gz, one fit per line, sorted by
fit_time_ns + compute_ns. Fields (SPEC §4): fit_time_ns, compute_ns, expiry_ms, beta, alpha,
rho, nu, forward, rmse_vol (unweighted, vol as a decimal), plus n_quotes, t_years and the
error against Deribit mark IV over the fitted strikes in vol points: mark_err_med_vp,
mark_err_max_vp, the nu bound used (nu_max) and at_bound (nu or rho at its bound).

Run:
  uv run --env-file .env python sabr_sidecar/fit.py 2026-09-29 2026-09-30 --beta 0.5 --jobs 2
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import os
import statistics
import sys
import time
import zlib
from datetime import date, timedelta
from multiprocessing import Pool
from pathlib import Path

from sabr.calibration import calibrate
from sabr.model import smile

DATA_DIR = Path(os.environ.get("DATA_DIR", "data"))
CACHE_DIR = Path(os.environ.get("CACHE_DIR", "cache"))

YEAR_NS = 365 * 86_400 * 10**9
MAX_TICKER_AGE_S = 10.0
MIN_SPREAD = 0.0005  # 0.05 vol points: caps the weight of a zero-width quote
# Vol-of-vol bound: 5 at one year (sabr's default), growing as 1/sqrt(T). Smile steepness in
# strike goes roughly with nu / ATM vol, and expiries of 1-3 days need nu far above 5.
NU_MAX_1Y = 5.0


def read_lines(path: Path):
    """Complete lines of a gzip file; stops cleanly at an unfinished tail (live hour)."""
    with gzip.open(path, "rt") as fh:
        try:
            for line in fh:
                if line.endswith("\n"):
                    yield line
        except EOFError, gzip.BadGzipFile, zlib.error:
            return


def is_instrument_list(line: str) -> bool:
    return '"result":[{' in line and '"instrument_name"' in line


def load_defs(msg: dict, defs: dict) -> None:
    for r in msg["result"]:
        if r.get("kind") == "option":
            defs[r["instrument_name"]] = {
                "expiry_ms": r["expiration_timestamp"],
                "strike": float(r["strike"]),
                "call": r["option_type"] == "call",
            }


def preload_defs(day: str) -> dict:
    """Instrument data in force at the start of `day`: the last instrument list recorded before
    it (the recorder refreshes at HH:01, so the day's own files have none before 00:01)."""
    defs: dict = {}
    prev = (date.fromisoformat(day) - timedelta(days=1)).isoformat()
    for path in sorted((DATA_DIR / "raw" / prev).glob("*.jsonl.gz"), reverse=True):
        last = None
        for line in read_lines(path):
            if is_instrument_list(line):
                last = line
        if last:
            load_defs(json.loads(last)["msg"], defs)
            break
    return defs


def fit_expiry(expiry_ms, quotes, fit_time_ns, beta, warm):
    """quotes: list of (strike, call, ticker). Returns a fit dict or None."""
    fwds = [q[2]["underlying_price"] for q in quotes if q[2].get("underlying_price")]
    if not fwds:
        return None
    fwd = statistics.median(fwds)
    strikes, vols, weights, marks = [], [], [], []
    for strike, call, tk in quotes:
        if call != (strike >= fwd):
            continue  # in the money
        bid, ask = tk.get("bid_iv") or 0.0, tk.get("ask_iv") or 0.0
        if bid <= 0 or ask <= 0 or ask < bid:
            continue
        strikes.append(strike)
        vols.append((bid + ask) / 200.0)
        weights.append(1.0 / max((ask - bid) / 100.0, MIN_SPREAD))
        marks.append((tk.get("mark_iv") or math.nan) / 100.0)
    if len(strikes) < ARGS.min_quotes:
        return None
    t_years = (expiry_ms * 10**6 - fit_time_ns) / YEAR_NS
    t0 = time.perf_counter_ns()
    try:
        nu_max = NU_MAX_1Y / math.sqrt(t_years)
        fit = calibrate(
            fwd, strikes, vols, t_years, beta=beta, x0=warm, weights=weights, nu_max=nu_max
        )
    except ValueError:
        return None
    compute_ns = time.perf_counter_ns() - t0
    model = smile(fwd, strikes, t_years, fit["alpha"], beta, fit["rho"], fit["nu"])
    errs = [abs(m - k) * 100 for m, k in zip(model, marks, strict=True) if not math.isnan(k)]
    return {
        "fit_time_ns": fit_time_ns,
        "compute_ns": compute_ns,
        "expiry_ms": expiry_ms,
        "beta": beta,
        "alpha": float(fit["alpha"]),
        "rho": float(fit["rho"]),
        "nu": float(fit["nu"]),
        "forward": fwd,
        "rmse_vol": fit["rmse"],
        "n_quotes": len(strikes),
        "nu_max": nu_max,
        # bool(): comparing numpy floats gives numpy.bool_, which json.dumps rejects.
        "at_bound": bool(fit["nu"] >= 0.999 * nu_max or abs(fit["rho"]) >= 0.998),
        "t_years": t_years,
        "mark_err_med_vp": statistics.median(errs) if errs else None,
        "mark_err_max_vp": max(errs) if errs else None,
    }


def fit_all(fit_time_ns, tickers, defs, warm, out, beta):
    min_ns = fit_time_ns + int(ARGS.min_hours * 3600 * 10**9)
    oldest_ns = fit_time_ns - int(MAX_TICKER_AGE_S * 10**9)
    by_expiry: dict[int, list] = {}
    for name, tk in tickers.items():
        d = defs.get(name)
        if d is None or tk["ts_ns"] < oldest_ns or d["expiry_ms"] * 10**6 <= min_ns:
            continue
        by_expiry.setdefault(d["expiry_ms"], []).append((d["strike"], d["call"], tk))
    for expiry_ms in sorted(by_expiry):
        fit = fit_expiry(expiry_ms, by_expiry[expiry_ms], fit_time_ns, beta, warm.get(expiry_ms))
        if fit is not None:
            warm[expiry_ms] = (fit["alpha"], fit["rho"], fit["nu"])
            out.append(fit)


def run_day(day: str) -> str:
    beta = ARGS.beta
    every_ns = int(ARGS.every * 10**9)
    files = sorted((DATA_DIR / "raw" / day).glob("*.jsonl.gz"))
    if not files:
        return f"{day}: no raw files in {DATA_DIR / 'raw' / day}"
    defs = preload_defs(day)
    tickers: dict[str, dict] = {}
    warm: dict[int, tuple] = {}
    out: list[dict] = []
    next_fit = None
    t_start = time.monotonic()
    for path in files:
        for line in read_lines(path):
            if is_instrument_list(line):
                load_defs(json.loads(line)["msg"], defs)
                continue
            if '"channel":"ticker.BTC-' not in line or "PERPETUAL" in line:
                continue
            data = json.loads(line)["msg"]["params"]["data"]
            ts_ns = data["timestamp"] * 10**6
            if next_fit is None:
                next_fit = (ts_ns // every_ns + 1) * every_ns
            if ts_ns > next_fit:
                # Fit at the latest grid time before this ticker, with everything seen so far.
                # Skipped grid times (e.g. a disconnect) would have the same inputs.
                fit_at = ((ts_ns - 1) // every_ns) * every_ns
                fit_all(fit_at, tickers, defs, warm, out, beta)
                next_fit = fit_at + every_ns
            tickers[data["instrument_name"]] = {
                "ts_ns": ts_ns,
                "bid_iv": data.get("bid_iv"),
                "ask_iv": data.get("ask_iv"),
                "mark_iv": data.get("mark_iv"),
                "underlying_price": data.get("underlying_price"),
            }
    out.sort(key=lambda f: (f["fit_time_ns"] + f["compute_ns"], f["expiry_ms"]))
    dest = CACHE_DIR / "sabr" / f"beta-{beta}" / f"{day}.jsonl.gz"
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(".tmp")
    with gzip.open(tmp, "wt") as fh:
        for f in out:
            fh.write(json.dumps(f, separators=(",", ":")) + "\n")
    tmp.replace(dest)
    med_ms = statistics.median(f["compute_ns"] for f in out) / 1e6 if out else float("nan")
    return (
        f"{day}: {len(out)} fits, median compute {med_ms:.1f} ms, "
        f"{time.monotonic() - t_start:.0f} s -> {dest}"
    )


def init(args: argparse.Namespace) -> None:
    global ARGS
    ARGS = args


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("days", nargs="+", help="YYYY-MM-DD (complete days only)")
    p.add_argument("--beta", type=float, required=True)
    p.add_argument("--every", type=float, default=5.0, help="seconds between fits")
    p.add_argument("--min-quotes", type=int, default=4)
    p.add_argument("--min-hours", type=float, default=0.5, help="skip expiries closer than this")
    p.add_argument("--jobs", type=int, default=1, help="days fitted in parallel")
    args = p.parse_args()
    init(args)
    if args.jobs > 1:
        with Pool(args.jobs, initializer=init, initargs=(args,)) as pool:
            for msg in pool.imap(run_day, args.days):
                print(msg, flush=True)
    else:
        for day in args.days:
            print(run_day(day), flush=True)
    sys.exit(0)


ARGS: argparse.Namespace

if __name__ == "__main__":
    main()
