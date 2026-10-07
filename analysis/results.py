"""Build the M5 results tables from the run folders under runs/m5/.

  strategy_table  report window: 4 strategies x 2 fill models x the latency sweep
                  (Whalley-Wilmott hedger)
  hedger_table    report window: 4 strategies x 4 hedgers (queue-aware, lowest latency)
  tuning_table    tuning window: every parameter set tried. In-sample, not a result.

Each table is written twice to runs/m5/results/:
  .csv  one row per account with every condition as its own column
  .md   the same rows for reading: conditions shared by all rows are listed once above the
        table, the ones that differ stay as columns

All PnL figures are after fees, in BTC; USD uses the index at the end of the run.
  max_drawdown_btc     largest peak-to-trough fall of PnL over the run's pnl.csv rows
  delta_explained_btc  sum over pnl.csv rows of (delta at the previous row) x (index return):
                       the part of the PnL a perpetual hedge at every row would have removed.
                       Shown for unhedged accounts.

  uv run --env-file .env python analysis/results.py
"""

from __future__ import annotations

import csv
import json
import os
import sys
from pathlib import Path

RUNS_DIR = Path(os.environ.get("RUNS_DIR", "runs")) / "m5"
OUT_DIR = RUNS_DIR / "results"

# Columns that say what was run. Everything a row needs to be reproduced or judged.
CONDITIONS = [
    "window",
    "trade_from_utc",
    "trade_until_utc",
    "instruments_quoted",
    "strategy",
    "tuning_pick",
    "strategy_params",
    "fair_value_source",
    "sabr_beta",
    "quote_size",
    "max_position",
    "max_vega_usd_per_vp",
    "abs_delta_range",
    "fee_edge_mult",
    "requote_band_ticks",
    "reduce_only_before_expiry_s",
    "no_quote_before_expiry_s",
    "hedger",
    "hedger_params",
    "fill_model",
    "trade_through",
    "md_timing",
    "md_latency_ms",
    "order_latency_ms",
    "recorded_latency_ms",
    "fee_schedule_date",
    "fee_tier",
    "starting_equity_btc",
]
RESULTS = [
    "pnl_btc",
    "pnl_usd",
    "spread_btc",
    "fees_btc",
    "adverse_60s_btc",
    "option_mtm_btc",
    "hedge_btc",
    "max_drawdown_btc",
    "delta_explained_btc",
    "maker_fills",
    "option_volume",
    "hedge_orders",
    "perp_volume_usd",
    "edge_at_fill_btc",
    "markout_1s_btc",
    "markout_10s_btc",
    "markout_60s_btc",
    "orders_placed",
    "orders_replaced",
    "accounting_residual_btc",
    "run",
    "account",
]


def series_stats(pnl_csv: Path) -> tuple[float, float]:
    """(max drawdown, delta-explained PnL), both BTC, from one account's pnl.csv."""
    peak = 0.0
    drawdown = 0.0
    explained = 0.0
    prev = None  # (index, delta) of the previous row that had an index
    with pnl_csv.open() as fh:
        for row in csv.DictReader(fh):
            pnl = float(row["pnl_btc"])
            peak = max(peak, pnl)
            drawdown = max(drawdown, peak - pnl)
            if not row["index_usd"]:
                continue
            index, delta = float(row["index_usd"]), float(row["delta_btc"])
            if prev is not None:
                explained += prev[1] * (index - prev[0]) / prev[0]
            prev = (index, delta)
    return drawdown, explained


def pick(window: str, account: str) -> str:
    """Which frozen parameter set an account runs (see run_grid.py): the best-PnL set, or the
    best-PnL set among those active enough on the tuning window."""
    if window != "report":
        return "grid"
    return "best_pnl_min_fills" if "-active" in account else "best_pnl"


def load(window: str, run_dir: Path) -> list[dict]:
    rows = []
    for summary_path in sorted(run_dir.glob("*/summary.json")):
        s = json.loads(summary_path.read_text())
        c, q, p = s["conditions"], s["conditions"]["quoter"], s["pnl_btc"]
        m = s["markouts_btc_per_contract"]
        drawdown, explained = series_stats(summary_path.with_name("pnl.csv"))
        rows.append(
            {
                "window": window,
                "trade_from_utc": c["trade_from_utc"],
                "trade_until_utc": c["trade_until_utc"],
                "instruments_quoted": c["instruments_quoted"],
                "strategy": c["strategy"],
                "tuning_pick": pick(window, s["account"]),
                "strategy_params": json.dumps(c["params"], sort_keys=True),
                "fair_value_source": c["fair_value_source"],
                "sabr_beta": c["sabr_beta"],
                "quote_size": q["size"],
                "max_position": q["max_position"],
                "max_vega_usd_per_vp": q["max_vega_usd_per_vp"],
                "abs_delta_range": f"{q['min_abs_delta']}-{q['max_abs_delta']}",
                "fee_edge_mult": q["fee_edge_mult"],
                "requote_band_ticks": q["requote_band_ticks"],
                "reduce_only_before_expiry_s": q["reduce_only_before_expiry_s"],
                "no_quote_before_expiry_s": q["no_quote_before_expiry_s"],
                "hedger": c["hedger"],
                "hedger_params": json.dumps(c["hedger_params"], sort_keys=True),
                "fill_model": c["fill_model"],
                "trade_through": c["trade_through"],
                "md_timing": c["md_timing"],
                "md_latency_ms": c["md_latency_ms"],
                "order_latency_ms": c["order_latency_ms"],
                "recorded_latency_ms": c["recorded_latency_ms"],
                "fee_schedule_date": c["fee_schedule_date"],
                "fee_tier": c["fee_tier"],
                "starting_equity_btc": s["starting_equity_btc"],
                "pnl_btc": p["total"],
                "pnl_usd": s["pnl_usd_at_last_index"],
                "spread_btc": p["spread"],
                "fees_btc": p["fees"],
                "adverse_60s_btc": p["adverse_selection_60s"],
                "option_mtm_btc": p["option_mtm"],
                "hedge_btc": p["hedge"],
                "max_drawdown_btc": drawdown,
                "delta_explained_btc": explained if c["hedger"] == "none" else None,
                "maker_fills": s["fills"]["maker"],
                "option_volume": s["fills"]["option_volume_contracts"],
                "hedge_orders": s.get("hedging", {}).get("orders", 0),
                "perp_volume_usd": s["fills"]["perp_volume_usd"],
                "edge_at_fill_btc": m["edge_at_fill"],
                "markout_1s_btc": m["after_1s"],
                "markout_10s_btc": m["after_10s"],
                "markout_60s_btc": m["after_60s"],
                "orders_placed": s["orders"]["placed"],
                "orders_replaced": s["orders"]["replaced"],
                "accounting_residual_btc": s["accounting_residual_btc"],
                "run": run_dir.name,
                "account": s["account"],
            }
        )
    return rows


def fmt(key: str, value) -> str:
    if value is None:
        return ""
    if key == "pnl_usd":
        return f"{value:+.2f}"
    if key.endswith("_btc") and key != "starting_equity_btc":
        return f"{value:+.6f}" if key != "max_drawdown_btc" else f"{value:.6f}"
    if isinstance(value, float):
        return f"{value:g}"
    return str(value)


def write(name: str, title: str, note: str, rows: list[dict], shown: list[str]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with (OUT_DIR / f"{name}.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=CONDITIONS + RESULTS)
        w.writeheader()
        w.writerows(rows)

    # Conditions every row shares are stated once; the rest are columns.
    common = [k for k in CONDITIONS if len({json.dumps(r[k]) for r in rows}) == 1]
    varying = [k for k in CONDITIONS if k not in common]
    columns = varying + [k for k in shown if k not in varying]
    lines = [f"## {title}", "", note, "", f"Conditions shared by all {len(rows)} rows:", ""]
    lines += [f"- {k}: {fmt(k, rows[0][k])}" for k in common]
    lines += ["", "| " + " | ".join(columns) + " |", "|" + "---|" * len(columns)]
    lines += ["| " + " | ".join(fmt(k, r[k]) for k in columns) + " |" for r in rows]
    (OUT_DIR / f"{name}.md").write_text("\n".join(lines) + "\n")
    print(f"{name}: {len(rows)} rows -> {OUT_DIR / name}.csv, .md")


def main() -> None:
    report = RUNS_DIR / "report"
    tune = RUNS_DIR / "tune"
    shown = [
        "pnl_btc",
        "pnl_usd",
        "spread_btc",
        "fees_btc",
        "adverse_60s_btc",
        "option_mtm_btc",
        "hedge_btc",
        "max_drawdown_btc",
        "maker_fills",
        "hedge_orders",
    ]
    made = False
    if report.exists():
        strategy_rows = []
        for run_dir in sorted(report.iterdir()):
            if run_dir.is_dir() and run_dir.name != "hedgers":
                strategy_rows += load("report", run_dir)
        if strategy_rows:
            strategy_rows.sort(
                key=lambda r: (r["strategy"], r["tuning_pick"], r["fill_model"], r["md_latency_ms"])
            )
            write(
                "strategy_table",
                "Strategy table (report window, out of sample)",
                "4 strategies x 2 fill models x the latency sweep, Whalley-Wilmott hedger. "
                "Parameters were fixed on the tuning window before these runs.",
                strategy_rows,
                shown,
            )
            made = True
        hedger_rows = load("report", report / "hedgers") if (report / "hedgers").exists() else []
        if hedger_rows:
            hedger_rows.sort(key=lambda r: (r["strategy"], r["hedger"]))
            write(
                "hedger_table",
                "Hedger table (report window, out of sample)",
                "4 strategies x 4 hedgers, queue-aware fills, lowest measured latency. "
                "Parameters were fixed on the tuning window before this run.",
                hedger_rows,
                shown + ["delta_explained_btc", "perp_volume_usd"],
            )
            made = True
    if tune.exists():
        tune_rows = []
        for run_dir in sorted(tune.iterdir()):
            if run_dir.is_dir():
                tune_rows += load("tuning", run_dir)
        if tune_rows:
            tune_rows.sort(key=lambda r: (r["strategy"], r["run"], r["account"]))
            write(
                "tuning_table",
                "Tuning table (tuning window, IN-SAMPLE: not a result)",
                "Every parameter set tried. Runs a00.. are stage A (parameter sets); b00 is "
                "stage B (Whalley-Wilmott risk aversion on the picked sets).",
                tune_rows,
                [
                    "pnl_btc",
                    "fees_btc",
                    "max_drawdown_btc",
                    "maker_fills",
                    "hedge_orders",
                    "account",
                ],
            )
            made = True
    if not made:
        sys.exit(f"no runs under {RUNS_DIR}: run analysis/run_grid.py first")


if __name__ == "__main__":
    main()
