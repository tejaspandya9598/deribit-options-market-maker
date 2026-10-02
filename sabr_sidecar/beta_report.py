"""Compare SABR fit error against Deribit mark IV for two betas, to pick beta.

Reads cache/sabr/beta-<b>/DAY.jsonl.gz for each beta and day (built by fit.py) and reports,
in vol points, over all fits:
  - median of each fit's median strike error vs mark IV
  - median, 95th percentile and max of each fit's worst-strike error vs mark IV
  - median unweighted RMSE against the quoted mid IVs
  - share of fits with nu or rho at its bound (a high share means the bounds, not the market,
    shape the fit)
Also per day, so one odd day is visible. Writes runs/sabr/beta_report.json.

Run:
  uv run --env-file .env python sabr_sidecar/beta_report.py 2026-09-29 2026-09-30
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
import statistics
from pathlib import Path

CACHE_DIR = Path(os.environ.get("CACHE_DIR", "cache"))
RUNS_DIR = Path(os.environ.get("RUNS_DIR", "runs"))


def pct(xs: list[float], q: float) -> float:
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * (len(xs) - 1) + 0.5))]


def summarize(fits: list[dict]) -> dict:
    med = [f["mark_err_med_vp"] for f in fits if f.get("mark_err_med_vp") is not None]
    worst = [f["mark_err_max_vp"] for f in fits if f.get("mark_err_max_vp") is not None]
    rmse = [f["rmse_vol"] * 100 for f in fits]
    if not fits:
        return {"fits": 0}
    return {
        "fits": len(fits),
        "median_strike_err_vp": statistics.median(med),
        "worst_strike_err_vp_median": statistics.median(worst),
        "worst_strike_err_vp_p95": pct(worst, 0.95),
        "worst_strike_err_vp_max": max(worst),
        "rmse_vs_mid_vp_median": statistics.median(rmse),
        "median_compute_ms": statistics.median(f["compute_ns"] for f in fits) / 1e6,
        "at_bound_share": sum(bool(f.get("at_bound")) for f in fits) / len(fits),
    }


def load(beta: str, day: str) -> list[dict]:
    path = CACHE_DIR / "sabr" / f"beta-{beta}" / f"{day}.jsonl.gz"
    with gzip.open(path, "rt") as fh:
        return [json.loads(line) for line in fh]


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("days", nargs="+")
    p.add_argument("--betas", nargs=2, default=["0.5", "1.0"])
    args = p.parse_args()

    report: dict = {"days": args.days, "betas": {}}
    for beta in args.betas:
        per_day = {day: load(beta, day) for day in args.days}
        report["betas"][beta] = {
            "all": summarize([f for fits in per_day.values() for f in fits]),
            "per_day": {day: summarize(fits) for day, fits in per_day.items()},
        }

    keys = [
        ("fits", "fits"),
        ("median_strike_err_vp", "median strike err (vp)"),
        ("worst_strike_err_vp_median", "worst strike err, median (vp)"),
        ("worst_strike_err_vp_p95", "worst strike err, p95 (vp)"),
        ("worst_strike_err_vp_max", "worst strike err, max (vp)"),
        ("rmse_vs_mid_vp_median", "RMSE vs mid, median (vp)"),
        ("median_compute_ms", "compute, median (ms)"),
        ("at_bound_share", "share of fits at a bound"),
    ]
    print(f"days: {', '.join(args.days)}  (errors vs Deribit mark IV, vol points)")
    print(f"{'':32}" + "".join(f"beta={b:<10}" for b in args.betas))
    for key, label in keys:
        row = "".join(f"{report['betas'][b]['all'][key]:<15.4g}" for b in args.betas)
        print(f"{label:32}{row}")

    out = RUNS_DIR / "sabr" / "beta_report.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=1))
    print(f"-> {out}")


if __name__ == "__main__":
    main()
