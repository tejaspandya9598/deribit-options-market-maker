from __future__ import annotations

import asyncio
import gzip
import json
import math
import socket
import subprocess
import sys
import time
from argparse import Namespace
from pathlib import Path
from urllib.request import urlopen

import pytest
from websockets.asyncio.client import connect
from websockets.exceptions import InvalidStatus

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from dashboard import server  # noqa: E402
from live import feed, replay  # noqa: E402
from recorder import record  # noqa: E402


def account(strategy: str = "fixed_spread", hedger: str = "none") -> dict:
    return {"strategy": strategy, "hedger": hedger}


def test_dashboard_build_config_accepts_mixed_strategies_and_hedgers() -> None:
    cfg = server.build_config(
        {"accounts": [account("fixed_spread", "fixed_band"), account("join_best", "time_based")]},
        "live",
        ["test live feed"],
    )
    assert [a["strategy"] for a in cfg["accounts"]] == ["fixed_spread", "join_best"]
    assert [a["hedger"] for a in cfg["accounts"]] == ["fixed_band", "time_based"]


@pytest.mark.parametrize(
    "req, message",
    [
        ({"accounts": [account("unknown")]}, "unknown strategy"),
        ({"accounts": [account("fixed_spread", "unknown")]}, "unknown hedger"),
        ({"accounts": [account()] * (server.MAX_ACCOUNTS + 1)}, "between 1 and 16"),
    ],
)
def test_dashboard_rejects_unknown_choices_and_too_many_accounts(req, message) -> None:
    with pytest.raises(ValueError, match=message):
        server.build_config(req, "live", [])


@pytest.mark.parametrize("latency", ["bad", -1, 60000.1, math.inf, math.nan])
def test_dashboard_rejects_invalid_latency(latency) -> None:
    with pytest.raises(ValueError):
        server.Session(server.Hub(), "live", {"accounts": [account()], "latency_ms": latency})


def test_dashboard_rejects_non_object_account_with_validation_error() -> None:
    # Invalid WebSocket payloads should be reported as a request error without dropping the page.
    with pytest.raises(ValueError):
        server.build_config({"accounts": [None]}, "live", [])


def test_catch_up_does_not_duplicate_fills_from_its_last_state() -> None:
    s = server.Session(server.Hub(), "live", {"accounts": [account()]})
    fill = {"a": 0, "t": 10, "i": "BTC-TEST", "side": "buy", "px": 0.01, "amt": 0.1,
            "maker": True, "fee": 0.0}
    state = {"type": "state", "t": 10, "accounts": [{"name": "one", "pnl": {"total": 0}}],
             "fills": [fill], "books": []}
    s.names = ["one"]
    s.history = [__import__("collections").deque([(1e-8, 0.0)], maxlen=server.HISTORY_POINTS)]
    s.fills.append(fill)
    s.last_state = json.dumps(state)
    messages = [json.loads(m) for m in s.catch_up()]
    all_fills = [f for m in messages if m.get("type") == "history" for f in m["fills"]]
    all_fills += [f for m in messages if m.get("type") == "state" for f in m["fills"]]
    assert all_fills == [fill]


def test_live_feed_stops_reconnecting_after_engine_pipe_closes(monkeypatch, tmp_path: Path) -> None:
    live = feed.LiveFeed(
        Namespace(url="ws://unused", beta=0.5, every=5.0), tmp_path / "session"
    )
    attempts = 0
    live.elog = lambda *_args, **_kwargs: None
    live.raw.event = lambda *_args, **_kwargs: None

    async def failed_session() -> None:
        nonlocal attempts
        attempts += 1
        if attempts == 2:
            asyncio.current_task().cancel()
        raise ExceptionGroup("reader task failed", [BrokenPipeError("engine closed stdin")])

    live._session = failed_session
    monkeypatch.setattr(record, "BACKOFF_MAX_S", 0.001)
    monkeypatch.setattr(record, "STABLE_S", 10_000)
    monkeypatch.setattr(record.random, "uniform", lambda low, _high: low)

    async def exercise() -> None:
        try:
            await live.run()
        except asyncio.CancelledError:
            pass

    asyncio.run(exercise())
    assert attempts == 1


def test_hub_refuses_start_while_another_session_is_running(tmp_path: Path, monkeypatch) -> None:
    engine = tmp_path / "mm_engine"
    engine.write_bytes(b"engine")
    monkeypatch.setattr(server, "ENGINE", engine)

    async def fake_start(session) -> None:
        session.status = "running"

    monkeypatch.setattr(server.Session, "start", fake_start)
    hub = server.Hub()

    async def exercise() -> None:
        await hub.command(
            {"cmd": "start", "mode": "live", "accounts": [account()], "latency_ms": 1}
        )
        with pytest.raises(ValueError, match="already running"):
            await hub.command(
                {"cmd": "start", "mode": "live", "accounts": [account()], "latency_ms": 1}
            )

    asyncio.run(exercise())


def test_live_start_without_probe_latency_does_not_lock_the_hub(
    tmp_path: Path, monkeypatch
) -> None:
    engine = tmp_path / "mm_engine"
    engine.write_bytes(b"engine")
    monkeypatch.setattr(server, "ENGINE", engine)
    monkeypatch.setattr(server, "RUNS_DIR", tmp_path / "runs")
    monkeypatch.setattr(server, "lowest_latency", lambda: None)
    real_pipe = server.os.pipe
    descriptors = real_pipe()
    monkeypatch.setattr(server.os, "pipe", lambda: descriptors)
    hub = server.Hub()

    async def exercise() -> None:
        with pytest.raises(ValueError, match="no recorded latency probes"):
            await hub.command({"cmd": "start", "mode": "live", "accounts": [account()]})
        assert hub.session is None

    try:
        asyncio.run(exercise())
    finally:
        for fd in descriptors:
            server.os.close(fd)


def test_session_pump_reports_engine_failure(tmp_path: Path) -> None:
    class FailedEngine:
        def __init__(self) -> None:
            self.stdout = self._lines()

        async def _lines(self):
            if False:
                yield b""

        async def wait(self) -> int:
            return 7

    class CapturingHub(server.Hub):
        def __init__(self) -> None:
            super().__init__()
            self.sent = []

        def send(self, obj: dict) -> None:
            self.sent.append(obj)

    async def exercise() -> server.Session:
        hub = CapturingHub()
        session = server.Session(hub, "live", {"accounts": [account()]})
        session.dir = tmp_path
        (tmp_path / "engine.log").write_text("fatal engine error\n")
        session.engine = FailedEngine()
        await session.pump()
        assert session.status == "ended"
        assert "code 7" in session.error and "fatal engine error" in session.error
        assert hub.sent[-1]["status"] == "ended"
        return session

    asyncio.run(exercise())


def test_dashboard_results_endpoint_data_matches_published_csvs() -> None:
    tables = server.results()["tables"]
    for name in ("strategy_table", "hedger_table", "tuning_table"):
        with (ROOT / "docs" / "results" / f"{name}.csv").open(newline="") as fh:
            import csv

            rows = list(csv.reader(fh))
        assert tables[name] == {"columns": rows[0], "rows": rows[1:]}


def test_engine_state_flag_keeps_run_files_identical_and_emits_end_line(tmp_path: Path) -> None:
    engine = ROOT / "build" / "mm_engine"
    if not engine.exists():
        pytest.skip("build the engine before running its integration test")
    config = tmp_path / "config.json"
    config.write_text(json.dumps({"accounts": [{"name": "one", "strategy": "fixed_spread"}]}))

    def run(name: str, state: bool) -> tuple[dict[str, bytes], str]:
        out = tmp_path / name
        args = [str(engine), "--config", str(config), "--run-dir", str(out)]
        if state:
            args += ["--state-interval-ms", "1000"]
        proc = subprocess.run(args, input=b"", capture_output=True, check=False)
        assert proc.returncode == 0, proc.stderr.decode()
        files = {str(p.relative_to(out)): p.read_bytes() for p in out.rglob("*") if p.is_file()}
        return files, proc.stdout.decode()

    plain_files, plain_stdout = run("without_state", False)
    state_files, state_stdout = run("with_state", True)
    assert state_files == plain_files
    assert plain_stdout == ""
    messages = [json.loads(line) for line in state_stdout.splitlines()]
    assert [m["type"] for m in messages] == ["hello", "state", "end"]
    assert messages[1]["fills"] == []


class _BufferStdout:
    def __init__(self) -> None:
        from io import BytesIO

        self.buffer = BytesIO()


@pytest.mark.parametrize("speed", [1, 10, 100])
def test_replay_pacing_outputs_identical_input_at_each_speed(monkeypatch, speed: int) -> None:
    records = [
        b'{"recv_ns":1000000000,"msg":{"n":0}}\n',
        b'{"recv_ns":11000000000,"msg":{"n":1}}\n',
        b'{"recv_ns":31000000000,"msg":{"n":2}}\n',
    ]
    now = [0.0]
    stdout = _BufferStdout()
    monkeypatch.setattr(replay, "lines", lambda _day: iter(records))
    monkeypatch.setattr(replay.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(replay.time, "sleep", lambda delay: now.__setitem__(0, now[0] + delay))
    monkeypatch.setattr(replay.sys, "argv", ["replay.py", "2026-09-29", "--speed", str(speed)])
    monkeypatch.setattr(replay.sys, "stdout", stdout)

    replay.main()

    assert stdout.buffer.getvalue() == b"".join(records)
    assert now[0] == pytest.approx(30 / speed)


def test_replay_speed_change_reanchors_the_current_message(monkeypatch, tmp_path: Path) -> None:
    control = tmp_path / "speed"
    control.write_text("100\n")
    records = [
        b'{"recv_ns":1000000000,"msg":{"n":0}}\n',
        b'{"recv_ns":21000000000,"msg":{"n":1}}\n',
        b'{"recv_ns":41000000000,"msg":{"n":2}}\n',
        b'{"recv_ns":51000000000,"msg":{"n":3}}\n',
    ]
    now = [0.0]
    changed = [False]

    def sleep(delay: float) -> None:
        now[0] += delay
        if now[0] >= 0.25 and not changed[0]:
            control.write_text("10\n")
            changed[0] = True

    stdout = _BufferStdout()
    monkeypatch.setattr(replay, "lines", lambda _day: iter(records))
    monkeypatch.setattr(replay.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(replay.time, "sleep", sleep)
    monkeypatch.setattr(
        replay.sys,
        "argv",
        ["replay.py", "2026-09-29", "--speed", "100", "--control", str(control)],
    )
    monkeypatch.setattr(replay.sys, "stdout", stdout)

    replay.main()

    assert changed[0]
    assert stdout.buffer.getvalue() == b"".join(records)
    assert now[0] >= 1.2  # 10 s of event time after the 100x -> 10x change


def test_replay_stops_at_an_unfinished_gzip_tail(tmp_path: Path, monkeypatch) -> None:
    day = "2026-09-29"
    data = tmp_path / "data"
    cache = tmp_path / "cache"
    checkpoint = cache / "books" / day / "00.jsonl"
    checkpoint.parent.mkdir(parents=True)
    checkpoint.write_bytes(b'{"checkpoint":true}\n')
    raw = data / "raw" / day
    raw.mkdir(parents=True)
    (raw / "00.jsonl.gz").write_bytes(gzip.compress(b'{"recv_ns":1,"msg":1}\n'))
    complete_member = gzip.compress(b'{"recv_ns":2,"msg":2}\n')
    unfinished_member = gzip.compress(b'{"recv_ns":3,"msg":"unfinished"}\n' + b"x" * 100)
    (raw / "01.jsonl.gz").write_bytes(complete_member + unfinished_member[:-15])
    monkeypatch.setattr(replay, "DATA_DIR", data)
    monkeypatch.setattr(replay, "CACHE_DIR", cache)

    got = list(replay.lines(day))

    assert got == [b'{"checkpoint":true}\n', b'{"recv_ns":1,"msg":1}\n',
                   b'{"recv_ns":2,"msg":2}\n']


def test_live_fitter_matches_exchange_clock_sidecar_on_recorded_sample(
    tmp_path: Path, monkeypatch
) -> None:
    from sabr_sidecar import fit

    day = "2026-09-29"
    source = ROOT / "data" / "raw" / day / "00.jsonl.gz"
    if not source.exists():
        pytest.skip("recorded market data is not included in a clean checkout")
    selected: list[str] = []
    messages: list[str] = []
    defs_seen = False
    start_ms = None
    with gzip.open(source, "rt") as fh:
        for line in fh:
            rec = json.loads(line)
            msg = rec.get("msg", {})
            result = msg.get("result")
            if isinstance(result, list) and result and "instrument_name" in result[0]:
                selected.append(line)
                messages.append(json.dumps(msg, separators=(",", ":")))
                defs_seen = True
                continue
            channel = msg.get("params", {}).get("channel", "")
            if not defs_seen or not channel.startswith("ticker.BTC-") or "PERPETUAL" in channel:
                continue
            ts = msg["params"]["data"]["timestamp"]
            if start_ms is None:
                start_ms = ts
            if ts > start_ms + 20_000:
                break
            selected.append(line)
            messages.append(json.dumps(msg, separators=(",", ":")))
    assert messages

    data = tmp_path / "data"
    day_dir = data / "raw" / day
    day_dir.mkdir(parents=True)
    with gzip.open(day_dir / "00.jsonl.gz", "wt") as fh:
        fh.writelines(selected)
    cache = tmp_path / "cache"
    monkeypatch.setattr(fit, "DATA_DIR", data)
    monkeypatch.setattr(fit, "CACHE_DIR", cache)
    monkeypatch.setattr(fit, "ARGS", Namespace(beta=0.5, clock="exchange", every=5.0,
                                                  min_quotes=4, min_hours=0.5), raising=False)
    expected_args = fit.ARGS
    result = fit.run_day(day)
    assert "fits" in result
    cache_file = cache / "sabr" / "beta-0.5" / f"{day}.jsonl.gz"
    with gzip.open(cache_file, "rt") as fh:
        expected = [json.loads(line) for line in fh]
    fitter = feed.Fitter(beta=0.5, every_s=5.0)
    actual = [json.loads(line) for raw in messages for line in fitter.feed(raw)]
    assert actual and len(actual) == len(expected)
    def order(fit: dict) -> tuple[int, int]:
        return fit["fit_time_ns"], fit["expiry_ms"]

    for got, want in zip(sorted(actual, key=order), sorted(expected, key=order), strict=True):
        for key in (
            "fit_time_ns", "expiry_ms", "beta", "alpha", "rho", "nu", "forward", "rmse_vol"
        ):
            assert got[key] == pytest.approx(want[key], rel=1e-12, abs=1e-12)
    assert fit.ARGS is expected_args


def test_dashboard_listener_rejects_foreign_websocket_origin() -> None:
    # Exercise the real local listeners without starting a trading session.
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    while True:
        with socket.socket() as probe:
            try:
                probe.bind(("127.0.0.1", port + 1))
                break
            except OSError:
                port += 2
    env = {**__import__("os").environ, "DASHBOARD_PORT": str(port)}
    proc = subprocess.Popen(
        [sys.executable, "dashboard/server.py"], cwd=ROOT, env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        for _ in range(100):
            try:
                with urlopen(f"http://127.0.0.1:{port}/api/meta", timeout=0.25) as response:
                    meta = json.loads(response.read())
                break
            except Exception:
                if proc.poll() is not None:
                    pytest.fail("dashboard server exited before becoming ready")
                time.sleep(0.05)
        else:
            pytest.fail("dashboard HTTP listener did not start")
        assert meta["ws_port"] == port + 1

        async def check_ws() -> None:
            async with connect(
                f"ws://127.0.0.1:{port + 1}", origin=f"http://127.0.0.1:{port}"
            ) as ws:
                assert json.loads(await ws.recv())["status"] == "none"
            with pytest.raises(InvalidStatus):
                async with connect(f"ws://127.0.0.1:{port + 1}", origin="http://evil.example"):
                    pass

        asyncio.run(check_ws())
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
