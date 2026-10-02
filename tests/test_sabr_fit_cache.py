from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from sabr_sidecar import fit


def test_fit_expiry_at_bound_record_is_json_serializable(monkeypatch):
    monkeypatch.setattr(fit, "ARGS", argparse.Namespace(min_quotes=4), raising=False)
    fit_time_ns = 1_790_640_000_000_000_000
    t_years = 0.1
    expiry_ms = int((fit_time_ns + t_years * fit.YEAR_NS) / 1_000_000)
    quotes = [
        (
            strike,
            True,
            {
                "underlying_price": 100.0,
                "bid_iv": 30.0 + offset,
                "ask_iv": 31.0 + offset,
                "mark_iv": 30.5 + offset,
            },
        )
        for offset, strike in enumerate((100.0, 105.0, 110.0, 115.0))
    ]

    row = fit.fit_expiry(expiry_ms, quotes, fit_time_ns, beta=0.5, warm=None)

    assert row is not None
    json.dumps(row)
