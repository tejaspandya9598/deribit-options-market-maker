from __future__ import annotations

import gzip
import importlib
import json
import sys
from argparse import Namespace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sabr_sidecar"))
fit = importlib.import_module("fit")


def test_recv_clock_fit_uses_only_tickers_received_by_fit_time(tmp_path, monkeypatch):
    day = "2026-09-29"
    raw = tmp_path / "raw" / day
    raw.mkdir(parents=True)
    records = [
        {
            "recv_ns": 6_000_000_000,
            "msg": {
                "params": {
                    "channel": "ticker.BTC-TEST",
                    "data": {
                        "timestamp": 5_500,
                        "instrument_name": "BTC-TEST-A",
                        "underlying_price": 100.0,
                    },
                }
            },
        },
        {
            "recv_ns": 11_000_000_000,
            "msg": {
                "params": {
                    "channel": "ticker.BTC-TEST",
                    "data": {
                        "timestamp": 9_000,
                        "instrument_name": "BTC-TEST-B",
                        "underlying_price": 100.0,
                    },
                }
            },
        },
    ]
    with gzip.open(raw / "00.jsonl.gz", "wt") as out:
        for record in records:
            out.write(json.dumps(record, separators=(",", ":")) + "\n")

    calls = []
    monkeypatch.setattr(fit, "DATA_DIR", tmp_path)
    monkeypatch.setattr(fit, "CACHE_DIR", tmp_path / "cache")
    monkeypatch.setattr(fit, "preload_defs", lambda _day: {})
    monkeypatch.setattr(
        fit,
        "fit_all",
        lambda at, tickers, _defs, _warm, _out, _beta: calls.append((at, set(tickers))),
    )
    monkeypatch.setattr(
        fit,
        "ARGS",
        Namespace(clock="recv", beta=0.5, every=5.0, min_quotes=4, min_hours=0.5),
        raising=False,
    )

    fit.run_day(day)

    assert calls == [(10_000_000_000, {"BTC-TEST-A"})]
