"""Tests for the M1 public recorder and book verifier."""

from __future__ import annotations

import asyncio
import gzip
import json
import sys
import time
from argparse import Namespace
from datetime import UTC, datetime
from pathlib import Path

import pytest
from websockets.asyncio.server import serve

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from recorder import check_book, record  # noqa: E402


@pytest.fixture(autouse=True)
def isolated_dirs(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    data_dir = tmp_path / "data"
    runs_dir = tmp_path / "runs"
    monkeypatch.setenv("DATA_DIR", str(data_dir))
    monkeypatch.setenv("RUNS_DIR", str(runs_dir))
    monkeypatch.setattr(check_book, "DATA_DIR", data_dir)
    monkeypatch.setattr(check_book, "RUNS_DIR", runs_dir)


def instrument(name: str, strike: float, expiry: int, *, active: bool = True) -> dict:
    return {
        "instrument_name": name,
        "strike": strike,
        "expiration_timestamp": expiry,
        "is_active": active,
    }


def test_select_universe_nearest_expiries_band_cap_and_both_rights() -> None:
    now = 1_000_000
    rows = []
    for expiry in (now + 100, now + 200, now + 300):
        for strike in (80, 90, 95, 100, 105, 110, 120):
            for right in ("C", "P"):
                rows.append(instrument(f"{expiry}-{strike}-{right}", strike, expiry))
    selected = record.select_universe(rows, 100, now, 2, 0.10, 3)
    expected_strikes = {95, 100, 105}
    assert selected == {
        row["instrument_name"]
        for row in rows
        if row["expiration_timestamp"] in (now + 100, now + 200)
        and row["strike"] in expected_strikes
    }
    assert any(name.endswith("-C") for name in selected)
    assert any(name.endswith("-P") for name in selected)


def test_select_universe_excludes_expired_and_inactive_and_handles_fewer_expiries() -> None:
    now = 1_000_000
    rows = [
        instrument("expired", 100, now, active=True),
        instrument("inactive", 100, now + 1, active=False),
        instrument("live", 100, now + 2),
    ]
    assert record.select_universe(rows, 100, now, 5, 0.15, 20) == {"live"}


def test_check_book_accepts_in_order_and_marks_only_one_gap_until_snapshot(
    tmp_path: Path,
) -> None:
    events: list[tuple[str, dict]] = []
    recorder = record.Recorder(Namespace(url="ws://unused"), tmp_path / "data")
    recorder.elog = lambda event, **fields: events.append((event, fields))
    recorder.raw.event = lambda kind, **fields: events.append((f"raw:{kind}", fields))
    recorder._check_book({"instrument_name": "X", "type": "snapshot", "change_id": 10})
    recorder._check_book(
        {"instrument_name": "X", "type": "change", "prev_change_id": 10, "change_id": 11}
    )
    assert recorder.last_change["X"] == 11
    assert recorder.counts["gaps"] == 0

    mismatch = {"instrument_name": "X", "type": "change", "prev_change_id": 8, "change_id": 12}
    recorder._check_book(mismatch)
    recorder._check_book({**mismatch, "change_id": 13})
    assert recorder.counts["gaps"] == 1
    assert recorder.resync_q.qsize() == 1
    assert [event for event, _ in events].count("gap") == 1

    recorder._check_book({"instrument_name": "X", "type": "snapshot", "change_id": 20})
    recorder._check_book(
        {"instrument_name": "X", "type": "change", "prev_change_id": 20, "change_id": 21}
    )
    assert recorder.last_change["X"] == 21
    assert recorder.counts["gaps"] == 1


def test_change_before_snapshot_counts_as_gap(tmp_path: Path) -> None:
    recorder = record.Recorder(Namespace(url="ws://unused"), tmp_path / "data")
    recorder.elog = lambda *_args, **_kwargs: None
    recorder.raw.event = lambda *_args, **_kwargs: None
    recorder._check_book(
        {"instrument_name": "X", "type": "change", "prev_change_id": 9, "change_id": 10}
    )
    assert recorder.counts["gaps"] == 1
    assert recorder.resync_q.qsize() == 1


def test_raw_writer_preserves_raw_json_emits_events_rotates_and_appends(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    events: list[tuple[str, dict]] = []
    writer = record.RawWriter(tmp_path, lambda kind, **fields: events.append((kind, fields)))
    hour1 = int(datetime(2026, 1, 2, 3, tzinfo=UTC).timestamp() * 1e9)
    raw = '{ "jsonrpc" : "2.0", "method":"test", "params": {"x": 1} }'
    writer.write(hour1, raw)
    monkeypatch.setattr(record.time, "time_ns", lambda: hour1 + 1)
    writer.event("probe", rtt_ms=2.5)
    writer.flush()
    first_path = tmp_path / "2026-01-02/03.jsonl.gz"
    lines = list(check_book.read_lines(first_path))
    assert json.loads(lines[0]) == {"recv_ns": hour1, "msg": json.loads(raw)}
    serialized_line = lines[0].rstrip("\n")
    assert serialized_line[serialized_line.index('"msg":') + len('"msg":') : -1] == raw
    assert json.loads(lines[1])["event"] == {"type": "probe", "rtt_ms": 2.5}

    writer.close()
    restarted = record.RawWriter(tmp_path, lambda *_args, **_kwargs: None)
    restarted.write(hour1 + 3_600_000_000_000, raw)
    writer2_path = tmp_path / "2026-01-02/04.jsonl.gz"
    restarted.close()
    assert writer2_path.exists()

    # A restart in the same hour appends another gzip member readable by one gzip reader.
    append = record.RawWriter(tmp_path, lambda *_args, **_kwargs: None)
    append.write(hour1, raw)
    append.close()
    with gzip.open(first_path, "rt") as fh:
        appended = fh.readlines()
    assert len(appended) == 3
    assert all(json.loads(line)["msg"] == json.loads(raw) for line in (appended[0], appended[2]))


def test_read_lines_tolerates_truncated_gzip(tmp_path: Path) -> None:
    path = tmp_path / "partial.jsonl.gz"
    with gzip.open(path, "wb") as fh:
        fh.write(b'{"complete":true}\n{"tail":')
    with path.open("r+b") as fh:
        fh.seek(-4, 2)
        fh.truncate()
    assert list(check_book.read_lines(path)) == ['{"complete":true}\n']


def test_ladder_entries_and_first_diff() -> None:
    side = {100.0: 2.0}
    check_book.ladder_entries(side, [["new", "99", 3], ["change", "100", 4]])
    assert side == {99.0: 3.0, 100.0: 4.0}
    check_book.ladder_entries(side, [["delete", "99", 0]])
    assert side == {100.0: 4.0}
    assert check_book.first_diff("bids", side, {100.0: 5.0}) == {
        "side": "bids",
        "price": 100.0,
        "stream": 4.0,
        "rest": 5.0,
    }
    assert check_book.first_diff("asks", side, side.copy()) is None


def _book_msg(
    kind: str,
    change_id: int,
    *,
    prev: int | None = None,
    bids=None,
    asks=None,
    timestamp: int | None = None,
) -> str:
    data = {"instrument_name": "BTC-X", "type": kind, "change_id": change_id}
    if prev is not None:
        data["prev_change_id"] = prev
    data["bids"] = bids or []
    data["asks"] = asks or []
    if timestamp is not None:
        data["timestamp"] = timestamp
    return json.dumps(
        {
            "jsonrpc": "2.0",
            "method": "subscription",
            "params": {"channel": "book.BTC-X.100ms", "data": data},
        },
        separators=(",", ":"),
    )


def _verify_case(
    tmp_path: Path, messages: list[str], targets: list[dict], *, expected_exit: int | None = None
) -> list[dict]:
    raw_dir = tmp_path / "data/raw/2026-01-02"
    raw_dir.mkdir(parents=True)
    raw_path = raw_dir / "03.jsonl.gz"
    with gzip.open(raw_path, "wt") as fh:
        for i, msg in enumerate(messages):
            fh.write(
                json.dumps(
                    {"recv_ns": 1_767_320_000_000_000_000 + i, "msg": json.loads(msg)},
                    separators=(",", ":"),
                )
                + "\n"
            )
    samples = tmp_path / "samples.jsonl"
    with samples.open("w") as fh:
        for sample in targets:
            fh.write(json.dumps(sample) + "\n")
    check_book.DATA_DIR = tmp_path / "data"
    args = Namespace(samples=str(samples), since="2026-01-02/03")
    exit_code = check_book.cmd_verify(args)
    assert exit_code in (0, 1)
    if expected_exit is not None:
        assert exit_code == expected_exit
    return json.loads(samples.with_suffix(".verify.json").read_text())["details"]


def test_verify_match_mismatch_unaligned_and_gap_before_target(tmp_path: Path) -> None:
    snapshot = _book_msg("snapshot", 100, bids=[["new", 10, 1]], asks=[["new", 11, 2]])
    change = _book_msg("change", 101, prev=100, bids=[["change", 10, 3]])
    good = {
        "local_ns": 1_767_320_000_000_000_000,
        "instrument": "BTC-X",
        "result": {"change_id": 101, "bids": [[10, 3]], "asks": [[11, 2]]},
    }
    assert [
        d["status"]
        for d in _verify_case(tmp_path / "match", [snapshot, change], [good], expected_exit=0)
    ] == ["match"]

    bad = {**good, "result": {**good["result"], "bids": [[10, 99]]}}
    assert [
        d["status"]
        for d in _verify_case(
            tmp_path / "mismatch", [snapshot, change], [bad], expected_exit=1
        )
    ] == ["mismatch"]

    unaligned_target = {**good, "result": {**good["result"], "change_id": 102}}
    batch = _book_msg("change", 103, prev=101, bids=[["change", 10, 4]])
    assert [
        d["status"]
        for d in _verify_case(
            tmp_path / "unaligned",
            [snapshot, change, batch],
            [unaligned_target],
            expected_exit=1,
        )
    ] == ["inside_batch"]

    gap = _book_msg("change", 105, prev=104, bids=[["change", 10, 5]])
    after_gap = {**good, "result": {**good["result"], "change_id": 106, "bids": [[10, 5]]}}
    recovery = _book_msg("change", 106, prev=105, bids=[["change", 10, 5]])
    assert [
        d["status"]
        for d in _verify_case(
            tmp_path / "gap", [snapshot, gap, recovery], [after_gap], expected_exit=1
        )
    ] == ["unexplained"]

    bracketed_by_gap = {**good, "result": {**good["result"], "change_id": 102}}
    broken_bracket = _book_msg("change", 103, prev=999, bids=[["change", 10, 5]])
    assert [
        d["status"]
        for d in _verify_case(
            tmp_path / "broken-bracket",
            [snapshot, change, broken_bracket],
            [bracketed_by_gap],
            expected_exit=1,
        )
    ] == ["unexplained"]


def test_bracketed_sample_with_two_untouched_level_diffs_is_unexplained(
    tmp_path: Path,
) -> None:
    snapshot = _book_msg(
        "snapshot",
        100,
        timestamp=1_000,
        bids=[["new", 10, 1], ["new", 9, 1], ["new", 8, 1]],
        asks=[["new", 11, 1], ["new", 12, 1], ["new", 13, 1]],
    )
    before = _book_msg("change", 101, prev=100, timestamp=1_010)
    after = _book_msg("change", 103, prev=101, timestamp=1_030, bids=[["change", 10, 2]])
    sample = {
        "local_ns": 1_767_320_000_000_000_000,
        "instrument": "BTC-X",
        "result": {
            "change_id": 102,
            "timestamp": 1_020,
            "bids": [[10, 1], [9, 9], [8, 1]],
            "asks": [[11, 1], [12, 9], [13, 1]],
        },
    }
    detail = _verify_case(tmp_path, [snapshot, before, after], [sample], expected_exit=1)[0]
    assert detail["status"] == "unexplained"
    assert detail["reason"] == "levels differ outside batch"
    assert detail["untouched_level_diffs"] == 2


def test_bracketed_sample_with_one_transient_level_is_inside_batch(tmp_path: Path) -> None:
    snapshot = _book_msg(
        "snapshot",
        100,
        timestamp=1_000,
        bids=[["new", 10, 1], ["new", 9, 1]],
        asks=[["new", 11, 1]],
    )
    before = _book_msg("change", 101, prev=100, timestamp=1_010)
    after = _book_msg("change", 103, prev=101, timestamp=1_030, bids=[["change", 10, 2]])
    # Price 9.5 is absent from both stream endpoint books and exists only in the REST sample.
    sample = {
        "local_ns": 1_767_320_000_000_000_000,
        "instrument": "BTC-X",
        "result": {
            "change_id": 102,
            "timestamp": 1_020,
            "bids": [[10, 1], [9.5, 0.25], [9, 1]],
            "asks": [[11, 1]],
        },
    }
    detail = _verify_case(tmp_path, [snapshot, before, after], [sample], expected_exit=1)[0]
    assert detail["status"] == "inside_batch"
    assert detail["untouched_level_diffs"] == 1


def test_rest_timestamp_outside_bracket_does_not_override_consistent_levels(
    tmp_path: Path,
) -> None:
    snapshot = _book_msg(
        "snapshot", 100, timestamp=1_000, bids=[["new", 10, 1]], asks=[["new", 11, 1]]
    )
    before = _book_msg("change", 101, prev=100, timestamp=1_010)
    after = _book_msg("change", 103, prev=101, timestamp=1_030)
    sample = {
        "local_ns": 1_767_320_000_000_000_000,
        "instrument": "BTC-X",
        "result": {
            "change_id": 102,
            "timestamp": 2_000,
            "bids": [[10, 1]],
            "asks": [[11, 1]],
        },
    }
    detail = _verify_case(tmp_path, [snapshot, before, after], [sample], expected_exit=1)[0]
    assert detail["status"] == "inside_batch"
    assert detail["untouched_level_diffs"] == 0
    assert detail["rest_ts"] == 2_000
    assert detail["stream_before_ts"] == 1_010
    assert detail["stream_after_ts"] == 1_030


def test_universe_refresh_keeps_sticky_options_and_removes_expired(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    recorder = record.Recorder(
        Namespace(url="ws://unused", expiries=1, band=0.15, max_strikes=20), tmp_path / "data"
    )
    old = instrument("old", 100, 2_000_000)
    expiring = instrument("expired", 100, 0)
    recorder.options = {"old": old, "expired": expiring}

    async def fake_call(method: str, _params: dict):
        stamp = 1
        if method == "public/get_instruments":
            return stamp, '{"result":[]}', [old, instrument("new", 100, 1_000_009)], 1
        if method == "public/get_instrument":
            return stamp, '{"result":{}}', {}, 1
        return stamp, "{}", {"index_price": 100}, 1

    recorder.call = fake_call
    recorder.raw.write = lambda *_args: None
    recorder.raw.event = lambda *_args, **_kwargs: None
    recorder.elog = lambda *_args, **_kwargs: None
    monkeypatch.setattr(record.time, "time_ns", lambda: 1_000_000_000_000)
    added, removed = asyncio.run(recorder._refresh_universe())
    assert added == ["new"]
    assert removed == ["expired"]
    assert set(recorder.options) == {"old", "new"}


def test_refresh_loop_unsubscribes_expired_and_subscribes_new_but_keeps_sticky(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    recorder = record.Recorder(
        Namespace(url="ws://unused", expiries=1, band=0.15, max_strikes=20), tmp_path / "data"
    )
    recorder.options = {"sticky": instrument("sticky", 100, 2_000_000)}
    recorder.elog = lambda *_args, **_kwargs: None
    rpc_calls: list[tuple[str, list[str]]] = []
    sleeps = 0

    async def refresh() -> tuple[list[str], list[str]]:
        recorder.options = {
            "sticky": instrument("sticky", 100, 2_000_000),
            "new": instrument("new", 100, 1_000_009),
        }
        return ["new"], ["expired"]

    async def fake_call(method: str, params: dict):
        rpc_calls.append((method, params["channels"]))
        return 0, "{}", params["channels"], 0

    class EndCycle(Exception):
        pass

    async def fake_sleep(seconds: float) -> None:
        nonlocal sleeps
        if seconds > 1:
            sleeps += 1
            if sleeps == 2:
                raise EndCycle

    recorder._refresh_universe = refresh
    recorder.call = fake_call
    monkeypatch.setattr(record, "SUB_PAUSE_S", 0)
    monkeypatch.setattr(record.asyncio, "sleep", fake_sleep)

    async def run_once() -> None:
        try:
            await recorder._refresh_loop()
        except EndCycle:
            pass

    asyncio.run(run_once())
    assert rpc_calls == [
        ("public/unsubscribe", record.channels_for("expired")),
        ("public/subscribe", record.channels_for("new")),
    ]
    assert set(recorder.options) == {"sticky", "new"}


def test_recorder_local_websocket_lifecycle(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Exercise startup RPCs, heartbeat test_request, probes, silence reconnect and resubscribe."""
    received: list[dict] = []
    event_log: list[dict] = []
    connections = 0
    answered_tests = 0

    async def handler(ws) -> None:
        nonlocal connections, answered_tests
        connections += 1
        async for text in ws:
            message = json.loads(text)
            received.append(message)
            method = message["method"]
            if method == "public/set_heartbeat":
                await ws.send(json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": 1}))
                await ws.send(
                    json.dumps(
                        {
                            "jsonrpc": "2.0",
                            "method": "heartbeat",
                            "params": {"type": "test_request"},
                        }
                    )
                )
            elif method == "public/get_instruments":
                now_ms = time.time_ns() // 1_000_000
                instruments = [
                    instrument(f"BTC-TEST-{strike}-{right}", strike, now_ms + 86_400_000)
                    for strike in range(86, 115)
                    for right in ("C", "P")
                ]
                await ws.send(
                    json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": instruments})
                )
            elif method == "public/get_instrument":
                await ws.send(
                    json.dumps(
                        {
                            "jsonrpc": "2.0",
                            "id": message["id"],
                            "result": {"instrument_name": "BTC-PERPETUAL"},
                        }
                    )
                )
            elif method == "public/get_index_price":
                await ws.send(
                    json.dumps(
                        {"jsonrpc": "2.0", "id": message["id"], "result": {"index_price": 100}}
                    )
                )
            elif method == "public/subscribe":
                await ws.send(
                    json.dumps(
                        {
                            "jsonrpc": "2.0",
                            "id": message["id"],
                            "result": message["params"]["channels"],
                        }
                    )
                )
            elif method == "public/test" and connections == 1:
                # Answer the heartbeat test_request and the first probe; then go silent.
                if sum(call["method"] == "public/test" for call in received) <= 2:
                    answered_tests += 1
                    await ws.send(
                        json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": "ok"})
                    )

    async def scenario() -> list[dict]:
        async with serve(handler, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            args = Namespace(url=f"ws://127.0.0.1:{port}", expiries=1, band=0.15, max_strikes=20)
            recorder = record.Recorder(args, tmp_path / "data")
            monkeypatch.setattr(record, "SILENCE_S", 0.08)
            monkeypatch.setattr(record, "RPC_TIMEOUT_S", 0.05)
            monkeypatch.setattr(record, "PROBE_S", 0.01)
            monkeypatch.setattr(record, "SUB_PAUSE_S", 0)
            monkeypatch.setattr(record, "BACKOFF_MAX_S", 0.01)
            monkeypatch.setattr(record, "STABLE_S", 10_000)
            monkeypatch.setattr(record.random, "uniform", lambda low, _high: low)
            task = asyncio.create_task(recorder.run())
            deadline = asyncio.get_running_loop().time() + 0.6

            def second_session_subscribed() -> bool:
                heartbeats = [
                    i for i, call in enumerate(received) if call["method"] == "public/set_heartbeat"
                ]
                return len(heartbeats) >= 2 and any(
                    call["method"] == "public/subscribe" for call in received[heartbeats[1] + 1 :]
                )

            while not second_session_subscribed() and asyncio.get_running_loop().time() < deadline:
                await asyncio.sleep(0.01)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
            recorder.close()
            log_path = next((tmp_path / "data/logs").glob("recorder-*.jsonl"))
            event_log.extend(json.loads(line) for line in log_path.read_text().splitlines())
        return received

    calls = asyncio.run(scenario())
    methods = [call["method"] for call in calls]
    assert "public/set_heartbeat" in methods
    assert methods.count("public/test") >= 1
    assert "public/get_instruments" in methods
    assert "public/get_instrument" in methods
    assert "public/subscribe" in methods
    assert connections >= 2
    raw_files = list((tmp_path / "data/raw").glob("*/*.jsonl.gz"))
    raw_text = "\n".join(line for path in raw_files for line in gzip.open(path, "rt"))
    assert "BTC-TEST-100-C" in raw_text
    assert '"instrument_name": "BTC-PERPETUAL"' in raw_text
    subscribe_calls = [call for call in calls if call["method"] == "public/subscribe"]
    assert all(len(call["params"]["channels"]) <= 100 for call in subscribe_calls)
    assert max(len(call["params"]["channels"]) for call in subscribe_calls) == 100
    assert answered_tests >= 1
    misses = [item for item in event_log if item["event"] == "heartbeat_miss"]
    reconnects = [item for item in event_log if item["event"] == "reconnect"]
    assert misses and misses[0]["silence_s"] == 0.08
    assert reconnects and reconnects[0]["delay_s"] > 0
    probes = [item for item in event_log if item["event"] == "probe"]
    assert probes and {"rtt_ms", "min_rtt_ms", "median_rtt_ms_1h"} <= probes[0].keys()
    second_start = [i for i, call in enumerate(calls) if call["method"] == "public/set_heartbeat"][
        1
    ]
    assert any(call["method"] == "public/subscribe" for call in calls[second_start + 1 :])
    assert any(
        call["method"] == "public/subscribe"
        and "book.BTC-TEST-100-C.100ms" in call["params"]["channels"]
        for call in calls[second_start:]
    )


def test_probe_sends_remain_scheduled_during_slow_round_trips(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    recorder = record.Recorder(Namespace(url="ws://unused"), tmp_path / "data")
    send_times: list[float] = []
    recorder.elog = lambda *_args, **_kwargs: None

    class StopProbe(Exception):
        pass

    class SlowWebSocket:
        async def send(self, text: str) -> None:
            call = json.loads(text)
            send_times.append(time.monotonic())
            if len(send_times) == 4:
                raise StopProbe

            async def reply_slowly() -> None:
                await asyncio.sleep(0.08)
                future = recorder.pending.get(call["id"])
                if future is not None and not future.done():
                    now_ns = time.perf_counter_ns()
                    future.set_result((time.time_ns(), now_ns, '{"result":"ok"}', {"result": "ok"}))

            asyncio.create_task(reply_slowly())

    recorder.ws = SlowWebSocket()
    monkeypatch.setattr(record, "PROBE_S", 0.01)
    monkeypatch.setattr(record, "RPC_TIMEOUT_S", 0.5)

    async def exercise() -> None:
        try:
            await recorder._probe_loop()
        except* StopProbe:
            pass

    asyncio.run(exercise())
    assert len(send_times) == 4
    assert all(
        later - earlier <= 0.05 for earlier, later in zip(send_times, send_times[1:], strict=False)
    )


def test_disconnect_bookkeeping_errors_do_not_stop_reconnect_loop(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, caplog: pytest.LogCaptureFixture
) -> None:
    recorder = record.Recorder(Namespace(url="ws://unused"), tmp_path / "data")
    attempts = 0

    def raising_elog(event: str, **_fields) -> None:
        if event in {"disconnect", "reconnect"}:
            raise OSError(f"cannot write {event}")

    async def failed_session() -> None:
        nonlocal attempts
        attempts += 1
        if attempts == 2:
            asyncio.current_task().cancel()
        raise ConnectionError("simulated disconnect")

    recorder.elog = raising_elog
    recorder.raw.event = lambda *_args, **_kwargs: (_ for _ in ()).throw(OSError("disk full"))
    recorder._session = failed_session
    monkeypatch.setattr(record, "BACKOFF_MAX_S", 0.001)
    monkeypatch.setattr(record, "STABLE_S", 100)
    monkeypatch.setattr(record.random, "uniform", lambda low, _high: low)

    async def exercise() -> None:
        try:
            await recorder.run()
        except asyncio.CancelledError:
            pass

    asyncio.run(exercise())
    assert attempts >= 2
    assert "bookkeeping failed" in caplog.text


def test_flush_error_is_logged_and_flusher_continues(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, caplog: pytest.LogCaptureFixture
) -> None:
    recorder = record.Recorder(Namespace(url="ws://unused"), tmp_path / "data")
    flushes = 0
    events: list[str] = []
    recorder.elog = lambda event, **_fields: events.append(event)

    def flush() -> None:
        nonlocal flushes
        flushes += 1
        if flushes == 1:
            raise OSError("disk temporarily unavailable")
        if flushes == 2:
            asyncio.current_task().cancel()

    recorder.raw.flush = flush
    monkeypatch.setattr(record, "FLUSH_S", 0.001)

    async def exercise() -> None:
        try:
            await recorder._flush_loop()
        except asyncio.CancelledError:
            pass

    with caplog.at_level("ERROR", logger="recorder"):
        asyncio.run(exercise())
    assert flushes == 2
    assert "flush failed" in caplog.text
    assert "flush_error" in events
