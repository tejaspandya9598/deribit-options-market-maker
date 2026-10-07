import json
import math
import shutil
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from analysis import run_grid  # noqa: E402
from analysis.results import series_stats  # noqa: E402
from analysis.run_grid import complete, parse_utc  # noqa: E402


def test_series_stats_uses_prior_delta_and_peak_to_trough_drawdown(tmp_path: Path) -> None:
    path = tmp_path / "pnl.csv"
    path.write_text(
        "time_ns,pnl_btc,delta_btc,index_usd\n"
        "1,0.000000,0.5,100\n"
        "2,0.010000,0.4,110\n"
        "3,0.005000,-0.2,100\n"
        "4,-0.020000,0.0,90\n"
    )
    drawdown, explained = series_stats(path)
    assert drawdown == 0.03
    expected = 0.5 * (110 - 100) / 100 + 0.4 * (100 - 110) / 110 - 0.2 * (90 - 100) / 100
    assert math.isclose(explained, expected, abs_tol=1e-15)


def completed_run(tmp_path: Path) -> tuple[Path, dict, tuple[str, str]]:
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    window = ("2026-01-01T00:00:00Z", "2026-01-02T00:00:00Z")
    config = {
        "fill_model": "queue_aware",
        "trade_until_utc": window[1],
        "trade_through": "trade_size",
        "quoter": {"stale_after_s": 10},
        "accounts": [
            {
                "name": "one",
                "strategy": "fixed_spread",
                "params": {"half_spread_vp": 4},
                "hedger": "fixed_band",
                "hedger_params": {"band_btc": 0.05},
                "quoter": {"size": 0.1},
            },
            {
                "name": "two",
                "strategy": "inventory_skew",
                "params": {"skew_vp_per_100_vega": 2},
                "hedger": "time_based",
                "hedger_params": {"interval_min": 15},
                "quoter": {"size": 0.2},
            },
        ],
    }
    # The engine records defaults and metadata in addition to every requested setting.
    saved = {
        "fill_model": "queue_aware",
        "trade_until_utc": window[1],
        "trade_through": "trade_size",
        "late_trade_window_ms": 1000,
        "quoter": {"stale_after_s": 10, "max_position": 1},
        "latency_source": "lowest probe RTT / 2",
        "md_latency_ms": 37.5,
        "order_latency_ms": 37.5,
        "accounts": [
            {**account, "quoter": {**account["quoter"], "max_position": 1}}
            for account in config["accounts"]
        ],
    }
    (run_dir / "config.json").write_text(json.dumps(saved))
    end_ns = int(parse_utc("2026-01-02T00:00:00Z").timestamp()) * 10**9
    for account in config["accounts"]:
        account_dir = run_dir / account["name"]
        account_dir.mkdir()
        (account_dir / "summary.json").write_text(
            json.dumps({"conditions": {"last_time_ns": end_ns}})
        )
    return run_dir, config, window


def test_complete_reuses_only_matching_account_and_hedger_settings(tmp_path: Path) -> None:
    run_dir, config, window = completed_run(tmp_path)
    assert complete(run_dir, config, window, 37.5)

    changed_account = json.loads(json.dumps(config))
    changed_account["accounts"][0]["params"]["half_spread_vp"] = 5
    assert not complete(run_dir, changed_account, window, 37.5)

    changed_hedger = json.loads(json.dumps(config))
    changed_hedger["accounts"][1]["hedger_params"]["interval_min"] = 30
    assert not complete(run_dir, changed_hedger, window, 37.5)


def test_complete_checks_latency_and_account_order(tmp_path: Path) -> None:
    run_dir, config, window = completed_run(tmp_path)
    assert not complete(run_dir, config, window, 44.4)

    reordered = json.loads(json.dumps(config))
    reordered["accounts"].reverse()
    assert not complete(run_dir, reordered, window, 37.5)


def test_complete_needs_config_and_distinguishes_default_latency(tmp_path: Path) -> None:
    run_dir, config, window = completed_run(tmp_path)
    assert complete(run_dir, config, window)  # engine-selected latency, not explicit flags

    saved = json.loads((run_dir / "config.json").read_text())
    saved["latency_source"] = "flags"
    (run_dir / "config.json").write_text(json.dumps(saved))
    assert not complete(run_dir, config, window)

    (run_dir / "config.json").unlink()
    assert not complete(run_dir, config, window)


def test_launch_requires_current_engine_stamp_and_matching_config(tmp_path: Path, monkeypatch) -> None:
    source, config, window = completed_run(tmp_path)
    runs_dir = tmp_path / "runs" / "m5"
    engine = tmp_path / "mm_engine"
    engine.write_bytes(b"test engine build")
    monkeypatch.setattr(run_grid, "RUNS_DIR", runs_dir)
    monkeypatch.setattr(run_grid, "ENGINE", engine)
    monkeypatch.setattr(run_grid, "window_inputs", lambda _window: ([], [], []))
    calls = []

    def fake_run(*_args, **_kwargs):
        calls.append(True)
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(run_grid.subprocess, "run", fake_run)
    current_id = run_grid.engine_id()

    for name, initial_stamp in (("missing", None), ("different", "old-build")):
        run_dir = runs_dir / "tune" / name
        shutil.copytree(source, run_dir)
        stamp = runs_dir / "logs" / f"tune_{name}.engine"
        stamp.parent.mkdir(parents=True, exist_ok=True)
        if initial_stamp is not None:
            stamp.write_text(initial_stamp + "\n")

        result = run_grid.launch("tune", name, config, window, 37.5)
        assert result["ok"]
        assert stamp.read_text().strip() == current_id

    # A matching build stamp cannot make an output reusable when requested settings differ.
    run_dir = runs_dir / "tune" / "changed_config"
    shutil.copytree(source, run_dir)
    stamp = runs_dir / "logs" / "tune_changed_config.engine"
    stamp.write_text(current_id + "\n")
    changed = json.loads(json.dumps(config))
    changed["fill_model"] = "optimistic"
    result = run_grid.launch("tune", "changed_config", changed, window, 37.5)
    assert not result["ok"]  # mocked run leaves the old config in place
    assert not stamp.exists()  # stale stamp was removed before the attempted rerun
    assert len(calls) == 3
