"""Run the M5 grid: tune on the first 2 days, then report on the last 5 with parameters frozen.

The 7-day recording is split in two (docs/SPEC.md 3, 7.11):
  tuning window  2026-09-28 21:42:41 -> 2026-09-30 21:42:41 UTC
  report window  2026-09-30 21:42:41 -> 2026-10-05 21:42:41 UTC

Steps (each is one command; run them in this order):

  tune     Tuning window, queue-aware fills, lowest measured latency, Whalley-Wilmott hedger.
           Stage A: every parameter set of GRIDS, 16 accounts per engine run (accounts are
           independent, so one pass over the data tests 16 sets). The best set per strategy is
           the one with the highest total PnL in BTC after fees.
           That rule can pick a set that hardly trades (a quote too wide to be hit loses
           nothing). So a second set is kept where it differs: the highest PnL among sets with
           at least MIN_ACTIVE_FILLS maker fills in the tuning window. Both are frozen and both
           are run in the report.
           Stage B: the four picked sets under each risk aversion in WW_RISK_AVERSIONS; the
           value with the highest PnL summed over the four strategies is kept.
           Writes config/m5_params.json. Nothing in the report window is read.
  report   Report window, parameters from config/m5_params.json, each run made once:
             - hedgers:  4 strategies x 4 hedgers in one 16-account run (queue-aware, lowest
                         latency); also the speed check for 16 accounts. Uses the active set
                         where a strategy has one (a set that never trades has nothing to hedge)
             - strategy: 4 strategies (Whalley-Wilmott) x 2 fill models x the latency sweep;
                         accounts "<strategy>" (best PnL) and "<strategy>-active"
           Refuses to start over existing report output. --resume continues an interrupted
           report (finished runs are reused, not repeated). To repeat runs after a code fix,
           delete runs/m5/report first and say so next to the results.

Outputs: runs/m5/<step>/<run>/ (engine run folders), runs/m5/configs/, runs/m5/logs/.
Tables come from analysis/results.py.

  uv run --env-file .env python analysis/run_grid.py tune --jobs 4
  uv run --env-file .env python analysis/run_grid.py report --jobs 4
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime, timedelta
from pathlib import Path

DATA_DIR = Path(os.environ.get("DATA_DIR", "data"))
CACHE_DIR = Path(os.environ.get("CACHE_DIR", "cache"))
RUNS_DIR = Path(os.environ.get("RUNS_DIR", "runs")) / "m5"
ENGINE = Path("build/release/mm_engine")
PARAMS_FILE = Path("config/m5_params.json")
BETA = 0.5

RECORDING_START = "2026-09-28T21:42:41Z"
TUNE_WINDOW = ("2026-09-28T21:42:41Z", "2026-09-30T21:42:41Z")
REPORT_WINDOW = ("2026-09-30T21:42:41Z", "2026-10-05T21:42:41Z")
RUN_OFF_S = 70  # the engine keeps going this long after the window (kRunOff)

STRATEGIES = ["fixed_spread", "mark_iv_follower", "inventory_skew", "join_best"]

# Shared by every account; never tuned.
BASE_QUOTER = {
    "size": 0.1,
    "max_position": 1.0,
    "max_vega_usd_per_vp": 100.0,
    "min_abs_delta": 0.10,
    "max_abs_delta": 0.90,
    "stale_after_s": 10,
    "no_quote_before_expiry_s": 3600,
    "requote_interval_ms": 100,
    "fit_max_age_s": 30,
}
BASE_RUN = {
    "trade_through": "trade_size",
    "late_trade_window_ms": 1000,
    "pnl_interval_s": 10,
    "settle_wait_s": 60,
    "starting_equity_btc": 1.0,
    "hedge_interval_ms": 1000,
    "hedge_slippage": 0.001,
}

# Tuning grids, fixed before any tuning run. "params" go to the strategy, "quoter" to the
# shared quoter settings. Every combination is one parameter set.
QUOTER_GRID = {
    "fee_edge_mult": [0, 1, 2],
    "requote_band_ticks": [0, 1],
    "reduce_only_before_expiry_s": [3600, 14400],
}
GRIDS = {
    "fixed_spread": {"params": {"half_spread_vp": [2, 4, 6]}, "quoter": QUOTER_GRID},
    "mark_iv_follower": {"params": {"half_spread_vp": [2, 4, 6]}, "quoter": QUOTER_GRID},
    "inventory_skew": {
        "params": {"half_spread_vp": [2, 4], "skew_vp_per_100_vega": [0.5, 2]},
        "quoter": {**QUOTER_GRID, "fee_edge_mult": [1, 2]},
    },
    "join_best": {"params": {"min_edge_vp": [1, 2, 4]}, "quoter": QUOTER_GRID},
}
WW_TUNING = {"risk_aversion_per_usd": 1e-3, "min_band_btc": 0.005}  # stage A
# The "active" pick: at least this many maker fills in the 2-day tuning window (10 a day).
# Added on 2026-10-06 after stage A showed the best-PnL sets of two strategies made 0 and 1
# fills; set before any report-window run.
MIN_ACTIVE_FILLS = 20
WW_RISK_AVERSIONS = [1e-4, 1e-3, 1e-2, 1e-1]  # stage B

# Not tuned.
FIXED_HEDGERS = {
    "none": {},
    "fixed_band": {"band_btc": 0.05},
    "time_based": {"interval_min": 15},
}
SWEEP_MS = [10, 25, 50, 75, 100, 250]  # plus the lowest and the median measured (SPEC 6)


def parse_utc(s: str) -> datetime:
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)


def window_inputs(window: tuple[str, str]) -> tuple[list[Path], list[Path], list[str]]:
    """(files piped to stdin in order, fit cache files, descriptions for config.json)."""
    start, end = parse_utc(window[0]), parse_utc(window[1]) + timedelta(seconds=RUN_OFF_S)
    hour = start.replace(minute=0, second=0)
    raw = []
    while hour <= end:
        path = DATA_DIR / "raw" / f"{hour:%Y-%m-%d}" / f"{hour:%H}.jsonl.gz"
        if not path.exists():
            sys.exit(f"missing recording {path}")
        raw.append(path)
        hour += timedelta(hours=1)
    first = []
    if window[0] != RECORDING_START:
        # Mid-recording start: the books as they stood at the top of the first hour.
        ckpt = CACHE_DIR / "books" / f"{start:%Y-%m-%d}" / f"{start:%H}.jsonl"
        if not ckpt.exists():
            sys.exit(f"missing book checkpoint {ckpt} (mm_engine --checkpoint-dir)")
        first = [ckpt]
    days = sorted({p.parent.name for p in raw})
    fits = [CACHE_DIR / "sabr" / f"beta-{BETA}-recv" / f"{d}.jsonl.gz" for d in days]
    for f in fits:
        if not f.exists():
            sys.exit(f"missing fit cache {f} (sabr_sidecar/fit.py {f.stem[:10]} --clock recv)")
    desc = [str(p) for p in first]
    desc.append(f"{raw[0]} .. {raw[-1]} ({len(raw)} hourly files)")
    desc.append(f"{fits[0]} .. {fits[-1]} ({len(fits)} daily files)")
    return first + raw, fits, desc


def measured_latencies() -> tuple[float, float]:
    """(lowest, median) one-way latency in ms from the recorder's probes over the 7 days."""
    rtts = []
    for path in sorted((DATA_DIR / "logs").glob("recorder-*.jsonl")):
        with path.open() as fh:
            for line in fh:
                if '"event": "probe"' not in line:
                    continue
                rec = json.loads(line)
                if "rtt_ms" in rec and rec["ts"] >= RECORDING_START[:-1]:
                    rtts.append(rec["rtt_ms"])
    if not rtts:
        sys.exit(f"no latency probes in {DATA_DIR / 'logs'}")
    return min(rtts) / 2, statistics.median(rtts) / 2


def product(grid: dict[str, list]) -> list[dict]:
    keys = list(grid)
    return [dict(zip(keys, combo, strict=True)) for combo in itertools.product(*grid.values())]


def account(name: str, strategy: str, params: dict, quoter: dict, hedger: str, hedger_params: dict):
    return {
        "name": name,
        "strategy": strategy,
        "params": params,
        "hedger": hedger,
        "hedger_params": hedger_params,
        "quoter": quoter,
    }


def run_config(window, fill_model: str, accounts: list[dict], desc: list[str]) -> dict:
    return {
        "inputs": desc,
        "fill_model": fill_model,
        **BASE_RUN,
        "trade_from_utc": window[0],
        "trade_until_utc": window[1],
        "quoter": BASE_QUOTER,
        "accounts": accounts,
    }


def same_settings(saved: dict, config: dict, latency_ms: float | None) -> bool:
    """True if `saved` (the config.json the engine wrote into a run folder) records the settings
    of `config`. The engine writes parameters with their defaults filled in, so every value
    asked for must be there and equal; extra saved keys are fine."""

    def covers(have, want) -> bool:
        if isinstance(want, dict):
            return isinstance(have, dict) and all(
                k in have and covers(have[k], v) for k, v in want.items()
            )
        return have == want

    top = {k: v for k, v in config.items() if k not in ("accounts", "quoter")}
    if not covers(saved, top):
        return False
    accounts = saved.get("accounts", [])
    if [a.get("name") for a in accounts] != [a["name"] for a in config["accounts"]]:
        return False
    if not all(covers(have, want) for have, want in zip(accounts, config["accounts"], strict=True)):
        return False
    if latency_ms is None:  # the engine's default: lowest probe round trip / 2
        return saved.get("latency_source", "flags") != "flags"
    return saved.get("md_latency_ms") == latency_ms and saved.get("order_latency_ms") == latency_ms


def complete(run_dir: Path, config: dict, window, latency_ms: float | None = None) -> bool:
    """True if `run_dir` holds a finished run of exactly `config`: its config.json records the
    same settings, and every account has a summary that reached the end of the window."""
    saved_path = run_dir / "config.json"
    if not saved_path.exists() or not same_settings(
        json.loads(saved_path.read_text()), config, latency_ms
    ):
        return False
    end_ns = int(parse_utc(window[1]).timestamp()) * 10**9
    for acc in config["accounts"]:
        path = run_dir / acc["name"] / "summary.json"
        if not path.exists():
            return False
        if json.loads(path.read_text())["conditions"]["last_time_ns"] < end_ns:
            return False
    return True


def engine_id() -> str:
    """SHA-256 of the engine binary: which build produced a run."""
    return hashlib.sha256(ENGINE.read_bytes()).hexdigest()


def launch(step: str, name: str, config: dict, window, latency_ms: float | None) -> dict:
    """Run one engine process, unless the finished output of the same settings and the same
    engine build is already there (the engine is deterministic, so it is reused, e.g. after an
    interrupted step; a rebuilt engine runs everything again).
    Returns {"name", "seconds" (None if reused), "ok"}."""
    stdin_files, fits, _ = window_inputs(window)
    cfg_path = RUNS_DIR / "configs" / f"{step}_{name}.json"
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    cfg_path.write_text(json.dumps(config, indent=2) + "\n")
    run_dir = RUNS_DIR / step / name
    log_path = RUNS_DIR / "logs" / f"{step}_{name}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    stamp_path = log_path.with_suffix(".engine")  # the build that made the run
    same_build = stamp_path.exists() and stamp_path.read_text().strip() == engine_id()
    if same_build and complete(run_dir, config, window, latency_ms):
        print(f"  {step}/{name}: already complete, reused", flush=True)
        return {"name": name, "seconds": None, "ok": True}
    stamp_path.unlink(missing_ok=True)

    plain = [str(p) for p in stdin_files if p.suffix != ".gz"]
    gz = [str(p) for p in stdin_files if p.suffix == ".gz"]
    feed = (
        f"cat {' '.join(plain)} <(gunzip -c {' '.join(gz)})"
        if plain
        else f"gunzip -c {' '.join(gz)}"
    )
    engine = f"{ENGINE} --config {cfg_path} --run-dir {run_dir} --md-timing received"
    if latency_ms is not None:
        engine += f" --md-latency-ms {latency_ms} --order-latency-ms {latency_ms}"
    engine += f" --fits <(gunzip -c {' '.join(str(f) for f in fits)})"
    env = {**os.environ, "DATA_DIR": str(DATA_DIR)}
    t0 = time.monotonic()
    # No pipefail: the engine stops reading at the end of the trading window, which ends the
    # feeding gunzip with a broken pipe. The engine's own exit code is the pipeline's, and a
    # feed that broke early shows up as a run that never reached the window's end.
    with log_path.open("w") as log:
        rc = subprocess.run(
            ["bash", "-c", f"{feed} | {engine}"], stderr=log, env=env, check=False
        ).returncode
    seconds = time.monotonic() - t0
    ok = rc == 0 and complete(run_dir, config, window, latency_ms)
    if ok:
        stamp_path.write_text(engine_id() + "\n")
    status = "ok" if ok else f"FAILED (exit {rc}), see {log_path}"
    print(f"  {step}/{name}: {status}, {seconds:.0f} s", flush=True)
    return {"name": name, "seconds": seconds, "ok": ok}


def run_all(step: str, jobs_list: list[tuple], jobs: int) -> list[dict]:
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(lambda j: launch(step, *j), jobs_list))
    if not all(r["ok"] for r in results):
        sys.exit("a run failed; nothing further was done")
    return results


def summary_of(step: str, run: str, name: str) -> dict:
    return json.loads((RUNS_DIR / step / run / name / "summary.json").read_text())


def pnl_of(step: str, run: str, name: str) -> float:
    return summary_of(step, run, name)["pnl_btc"]["total"]


# ---------------------------------------------------------------------------------------------


def tune(jobs: int) -> None:
    if PARAMS_FILE.exists():
        sys.exit(f"{PARAMS_FILE} exists: parameters are already frozen. Delete it to tune again.")
    _, _, desc = window_inputs(TUNE_WINDOW)

    # Stage A: every parameter set, 16 accounts per run.
    sets = []  # (account name, strategy, params, quoter)
    for strategy in STRATEGIES:
        grid = GRIDS[strategy]
        combos = [(p, q) for p in product(grid["params"]) for q in product(grid["quoter"])]
        for i, (p, q) in enumerate(combos):
            sets.append((f"{strategy}-{i:02d}", strategy, p, q))
    chunks = [sets[i : i + 16] for i in range(0, len(sets), 16)]
    print(f"stage A: {len(sets)} parameter sets in {len(chunks)} runs", flush=True)
    stage_a = []
    where = {}
    for n, chunk in enumerate(chunks):
        accounts = [
            account(name, s, p, {**BASE_QUOTER, **q}, "whalley_wilmott", WW_TUNING)
            for name, s, p, q in chunk
        ]
        run = f"a{n:02d}"
        for name, *_ in chunk:
            where[name] = run
        stage_a.append(
            (run, run_config(TUNE_WINDOW, "queue_aware", accounts, desc), TUNE_WINDOW, None)
        )
    run_all("tune", stage_a, jobs)

    tried = {s: [] for s in STRATEGIES}
    for name, strategy, p, q in sets:
        tried[strategy].append(
            {
                "name": name,
                "run": where[name],
                "params": p,
                "quoter": q,
                "pnl_btc": pnl_of("tune", where[name], name),
                "maker_fills": summary_of("tune", where[name], name)["fills"]["maker"],
            }
        )
    picked = {s: max(rows, key=lambda r: r["pnl_btc"]) for s, rows in tried.items()}
    active = {}  # only where it is a different set
    for s, rows in tried.items():
        busy = [r for r in rows if r["maker_fills"] >= MIN_ACTIVE_FILLS]
        if busy and picked[s]["maker_fills"] < MIN_ACTIVE_FILLS:
            active[s] = max(busy, key=lambda r: r["pnl_btc"])

    # Stage B: the picked sets under each risk aversion.
    accounts = []
    for s in STRATEGIES:
        for k, ra in enumerate(WW_RISK_AVERSIONS):
            accounts.append(
                account(
                    f"{s}-ra{k}",
                    s,
                    picked[s]["params"],
                    {**BASE_QUOTER, **picked[s]["quoter"]},
                    "whalley_wilmott",
                    {**WW_TUNING, "risk_aversion_per_usd": ra},
                )
            )
    print("stage B: Whalley-Wilmott risk aversion", flush=True)
    run_all(
        "tune",
        [("b00", run_config(TUNE_WINDOW, "queue_aware", accounts, desc), TUNE_WINDOW, None)],
        1,
    )
    by_ra = [
        {
            "risk_aversion_per_usd": ra,
            "pnl_btc": {s: pnl_of("tune", "b00", f"{s}-ra{k}") for s in STRATEGIES},
        }
        for k, ra in enumerate(WW_RISK_AVERSIONS)
    ]
    for row in by_ra:
        row["pnl_btc_sum"] = sum(row["pnl_btc"].values())
    best_ra = max(by_ra, key=lambda r: r["pnl_btc_sum"])

    frozen = {
        "note": "Tuned on the tuning window only (analysis/run_grid.py tune). Frozen before any "
        "report-window run; in-sample numbers below are not results.",
        "tuning_window_utc": list(TUNE_WINDOW),
        "report_window_utc": list(REPORT_WINDOW),
        "objective": "total PnL in BTC after fees over the tuning window; queue-aware fills, "
        "lowest measured latency, Whalley-Wilmott hedger, received-time market data",
        "active_rule": f"where the best-PnL set made fewer than {MIN_ACTIVE_FILLS} maker fills in "
        "the tuning window, 'active' is the best-PnL set among those with at least that many",
        "strategies": {
            s: {
                "params": picked[s]["params"],
                "quoter": picked[s]["quoter"],
                "in_sample_pnl_btc": picked[s]["pnl_btc"],
                "in_sample_maker_fills": picked[s]["maker_fills"],
                "sets_tried": len(tried[s]),
                "active": {
                    "params": active[s]["params"],
                    "quoter": active[s]["quoter"],
                    "in_sample_pnl_btc": active[s]["pnl_btc"],
                    "in_sample_maker_fills": active[s]["maker_fills"],
                }
                if s in active
                else None,
            }
            for s in STRATEGIES
        },
        "whalley_wilmott": {**WW_TUNING, "risk_aversion_per_usd": best_ra["risk_aversion_per_usd"]},
        "whalley_wilmott_tried": by_ra,
        "fixed_hedgers": FIXED_HEDGERS,
        "base_quoter": BASE_QUOTER,
        "base_run": BASE_RUN,
    }
    PARAMS_FILE.write_text(json.dumps(frozen, indent=2) + "\n")
    (RUNS_DIR / "tune" / "tried.json").write_text(json.dumps(tried, indent=2) + "\n")
    for s in STRATEGIES:
        for label, r in (("best PnL", picked[s]), ("active", active.get(s))):
            if r is not None:
                print(
                    f"  {s} [{label}]: {r['params']} {r['quoter']} in-sample "
                    f"{r['pnl_btc']:+.6f} BTC, {r['maker_fills']} fills ({len(tried[s])} sets)"
                )
    print(f"  risk aversion: {best_ra['risk_aversion_per_usd']}")
    print(f"frozen in {PARAMS_FILE}")


def report(jobs: int, resume: bool) -> None:
    if not PARAMS_FILE.exists():
        sys.exit(f"{PARAMS_FILE} is missing: run the tune step first")
    done = RUNS_DIR / "report"
    if done.exists() and any(done.iterdir()) and not resume:
        sys.exit(
            f"{done} already has runs. The report window is meant to be run once; "
            "pass --resume to continue an interrupted report."
        )
    frozen = json.loads(PARAMS_FILE.read_text())
    _, _, desc = window_inputs(REPORT_WINDOW)
    lowest, median = measured_latencies()
    ww = frozen["whalley_wilmott"]
    hedgers = {**frozen["fixed_hedgers"], "whalley_wilmott": ww}

    def strat_account(s: str, hedger: str, name: str, active: bool) -> dict:
        f = frozen["strategies"][s]["active"] if active else frozen["strategies"][s]
        return account(
            name, s, f["params"], {**BASE_QUOTER, **f["quoter"]}, hedger, hedgers[hedger]
        )

    has_active = {s: frozen["strategies"][s]["active"] is not None for s in STRATEGIES}

    # Hedger table: one 16-account run, on its own so its wall time is the speed check.
    accounts = [
        strat_account(s, h, f"{s}{'-active' if has_active[s] else ''}--{h}", has_active[s])
        for s in STRATEGIES
        for h in hedgers
    ]
    print("hedger table: 16 accounts, run alone for timing", flush=True)
    timing = run_all(
        "report",
        [
            (
                "hedgers",
                run_config(REPORT_WINDOW, "queue_aware", accounts, desc),
                REPORT_WINDOW,
                None,
            )
        ],
        1,
    )[0]
    days = (parse_utc(REPORT_WINDOW[1]) - parse_utc(REPORT_WINDOW[0])).total_seconds() / 86400
    if timing["seconds"] is not None:  # not a reused run
        speed = {
            "run": "hedgers",
            "accounts": len(accounts),
            "days": days,
            "wall_seconds": timing["seconds"],
            "seconds_per_day": timing["seconds"] / days,
            "note": "wall time of the whole pipeline (gunzip + engine), "
            "run with nothing else going",
        }
        (RUNS_DIR / "report" / "speed.json").write_text(json.dumps(speed, indent=2) + "\n")
        print(f"  {speed['seconds_per_day']:.0f} s per recorded day with {len(accounts)} accounts")

    # Strategy table: 2 fill models x the latency sweep, 4 accounts each.
    latencies = [("lowest", None), ("median", round(median, 3))] + [
        (f"{ms}ms", ms) for ms in SWEEP_MS
    ]
    jobs_list = []
    for fill in ("queue_aware", "optimistic"):
        for label, ms in latencies:
            accounts = [strat_account(s, "whalley_wilmott", s, False) for s in STRATEGIES]
            accounts += [
                strat_account(s, "whalley_wilmott", f"{s}-active", True)
                for s in STRATEGIES
                if has_active[s]
            ]
            jobs_list.append(
                (
                    f"{fill}_{label}",
                    run_config(REPORT_WINDOW, fill, accounts, desc),
                    REPORT_WINDOW,
                    ms,
                )
            )
    print(
        f"strategy table: {len(jobs_list)} runs (lowest {lowest:.3f} ms, median {median:.3f} ms)",
        flush=True,
    )
    run_all("report", jobs_list, jobs)


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("step", choices=["tune", "report"])
    p.add_argument("--jobs", type=int, default=4, help="engine runs side by side")
    p.add_argument("--resume", action="store_true", help="report: continue an interrupted step")
    args = p.parse_args()
    if not ENGINE.exists():
        sys.exit(
            f"{ENGINE} not found: cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release "
            "&& cmake --build build/release"
        )
    if args.step == "tune":
        tune(args.jobs)
    else:
        report(args.jobs, args.resume)


if __name__ == "__main__":
    main()
