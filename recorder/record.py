"""Record Deribit public market data to data/raw/.

Universe: BTC options in the nearest expiries with strikes near the index, plus BTC-PERPETUAL.
Channels: book / trades / ticker at 100ms per instrument, and the BTC price index.

Output:
  data/raw/YYYY-MM-DD/HH.jsonl.gz   one line per message: {"recv_ns":..,"msg":<raw Deribit JSON>}
                                    or a recorder event:  {"recv_ns":..,"event":{...}}
  data/logs/recorder-YYYY-MM-DD.jsonl   connects, gaps, resubscribes, heartbeat misses, probes, ...

Public endpoints only. No auth.

Run: uv run --env-file .env python recorder/record.py [--minutes N]
"""

from __future__ import annotations

import argparse
import asyncio
import gzip
import json
import logging
import os
import random
import signal
import statistics
import time
from collections import deque
from datetime import UTC, datetime
from pathlib import Path

from websockets.asyncio.client import ClientConnection, connect

PERP = "BTC-PERPETUAL"
INDEX_NAME = "btc_usd"
INDEX_CHANNEL = f"deribit_price_index.{INDEX_NAME}"

HEARTBEAT_S = 10  # Deribit minimum
SILENCE_S = 30  # no message at all for this long -> connection is dead
PROBE_S = 5  # acceptance: a probe at least every 10 s; 5 s leaves margin for timer jitter
FLUSH_S = 5
REFRESH_OFFSET_S = 60  # universe refresh at HH:01, just after the 08:00 UTC expiry
SUB_CHUNK = 100  # channels per subscribe call
SUB_PAUSE_S = 0.5  # subscribe allows ~3.3 calls/s sustained, burst 10
BACKOFF_MAX_S = 60
STABLE_S = 300  # a connection this long resets the backoff
RPC_TIMEOUT_S = 10
PROBE_WINDOW = 360  # probes kept for the median (1 hour)

log = logging.getLogger("recorder")


def utc(ns: int) -> datetime:
    return datetime.fromtimestamp(ns / 1e9, UTC)


class EventLog:
    """JSON-lines recorder log, one file per UTC day."""

    def __init__(self, log_dir: Path) -> None:
        self.dir = log_dir
        self.dir.mkdir(parents=True, exist_ok=True)
        self.day = ""
        self.fh = None

    def __call__(self, event: str, **fields) -> None:
        now = time.time_ns()
        day = utc(now).strftime("%Y-%m-%d")
        if day != self.day:
            if self.fh:
                self.fh.close()
            self.fh = open(self.dir / f"recorder-{day}.jsonl", "a", buffering=1)
            self.day = day
        rec = {"ts": utc(now).isoformat(timespec="milliseconds"), "event": event, **fields}
        self.fh.write(json.dumps(rec) + "\n")
        level = logging.DEBUG if event == "probe" else logging.INFO
        log.log(level, "%s %s", event, fields)

    def close(self) -> None:
        if self.fh:
            self.fh.close()
            self.fh = None


class RawWriter:
    """Hourly gzipped JSON-lines files under data/raw/YYYY-MM-DD/HH.jsonl.gz (UTC)."""

    def __init__(self, raw_dir: Path, elog: EventLog) -> None:
        self.dir = raw_dir
        self.elog = elog
        self.key = ""
        self.path: Path | None = None
        self.fh: gzip.GzipFile | None = None

    def _open_for(self, ns: int) -> None:
        t = utc(ns)
        key = t.strftime("%Y-%m-%d/%H")
        if key == self.key:
            return
        self._close()
        self.path = self.dir / f"{key}.jsonl.gz"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # Append mode: a restart within the hour adds a second gzip member, which readers handle.
        self.fh = gzip.open(self.path, "ab", compresslevel=6)
        self.key = key

    def _close(self) -> None:
        if self.fh is None:
            return
        self.fh.close()
        self.elog("rotate", file=str(self.path), bytes=self.path.stat().st_size)
        self.fh = None

    def write(self, recv_ns: int, raw: str) -> None:
        self._open_for(recv_ns)
        if "\n" in raw:  # JSON newlines are whitespace; keep one message per line
            raw = raw.replace("\n", " ")
        self.fh.write(f'{{"recv_ns":{recv_ns},"msg":{raw}}}\n'.encode())

    def event(self, kind: str, **fields) -> None:
        recv_ns = time.time_ns()
        self._open_for(recv_ns)
        body = json.dumps({"type": kind, **fields}, separators=(",", ":"))
        self.fh.write(f'{{"recv_ns":{recv_ns},"event":{body}}}\n'.encode())

    def flush(self) -> None:
        if self.fh:
            self.fh.flush()  # Z_SYNC_FLUSH: everything so far is readable from disk

    def close(self) -> None:
        self._close()
        self.key = ""


def select_universe(
    instruments: list[dict],
    index: float,
    now_ms: int,
    n_expiries: int,
    band: float,
    max_strikes: int,
) -> set[str]:
    """Nearest `n_expiries` expiries; per expiry the `max_strikes` strikes closest to the index
    within +/- band, calls and puts both."""
    live = [i for i in instruments if i["is_active"] and i["expiration_timestamp"] > now_ms]
    expiries = sorted({i["expiration_timestamp"] for i in live})[:n_expiries]
    chosen: set[str] = set()
    for exp in expiries:
        in_exp = [i for i in live if i["expiration_timestamp"] == exp]
        strikes = {i["strike"] for i in in_exp if abs(i["strike"] / index - 1.0) <= band}
        keep = set(sorted(strikes, key=lambda k: (abs(k - index), k))[:max_strikes])
        chosen |= {i["instrument_name"] for i in in_exp if i["strike"] in keep}
    return chosen


def channels_for(name: str) -> list[str]:
    return [f"book.{name}.100ms", f"trades.{name}.100ms", f"ticker.{name}.100ms"]


def chunks(xs: list[str], n: int) -> list[list[str]]:
    return [xs[i : i + n] for i in range(0, len(xs), n)]


class Recorder:
    def __init__(self, args: argparse.Namespace, data_dir: Path) -> None:
        self.args = args
        self.url = args.url
        self.elog = EventLog(data_dir / "logs")
        self.raw = RawWriter(data_dir / "raw", self.elog)
        self.ws: ClientConnection | None = None
        self.next_id = 1
        self.pending: dict[int, asyncio.Future] = {}
        # Options we record, with their instrument records. Sticky until expiry.
        self.options: dict[str, dict] = {}
        self.last_change: dict[str, int] = {}  # instrument -> last book change_id
        self.resyncing: set[str] = set()  # waiting for a fresh snapshot after a gap
        self.resync_q: asyncio.Queue[str] = asyncio.Queue()
        self.rtts: deque[float] = deque(maxlen=PROBE_WINDOW)
        self.min_rtt: float | None = None
        self.counts = {"messages": 0, "gaps": 0}

    # ---------- JSON-RPC over the one websocket ----------

    async def _send(self, method: str, params: dict) -> tuple[int, asyncio.Future | None, int]:
        rid = self.next_id
        self.next_id += 1
        fut = asyncio.get_running_loop().create_future()
        self.pending[rid] = fut
        sent = time.perf_counter_ns()
        await self.ws.send(
            json.dumps({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        )
        return rid, fut, sent

    async def call(self, method: str, params: dict) -> tuple[int, str, object, int]:
        """Returns (recv_ns, raw text, result, round trip ns)."""
        rid, fut, sent = await self._send(method, params)
        try:
            async with asyncio.timeout(RPC_TIMEOUT_S):
                recv_ns, recv_perf, raw, msg = await fut
        finally:
            self.pending.pop(rid, None)
        if "error" in msg:
            raise RuntimeError(f"{method} failed: {msg['error']}")
        return recv_ns, raw, msg["result"], recv_perf - sent

    async def _reader(self) -> None:
        while True:
            try:
                async with asyncio.timeout(SILENCE_S):
                    raw = await self.ws.recv()
            except TimeoutError:
                self.elog("heartbeat_miss", silence_s=SILENCE_S)
                raise
            recv_ns = time.time_ns()
            recv_perf = time.perf_counter_ns()
            msg = json.loads(raw)
            method = msg.get("method")
            if method == "subscription":
                self.raw.write(recv_ns, raw)
                self.counts["messages"] += 1
                params = msg["params"]
                if params["channel"].startswith("book."):
                    self._check_book(params["data"])
            elif method == "heartbeat":
                if msg["params"].get("type") == "test_request":
                    rid, _, _ = await self._send("public/test", {})
                    self.pending.pop(rid, None)
            elif "id" in msg:
                fut = self.pending.pop(msg["id"], None)
                if fut is not None and not fut.done():
                    fut.set_result((recv_ns, recv_perf, raw, msg))

    # ---------- gap detection ----------

    def _check_book(self, data: dict) -> None:
        name = data["instrument_name"]
        if data.get("type") == "snapshot":
            self.last_change[name] = data["change_id"]
            self.resyncing.discard(name)
            return
        if name in self.resyncing:
            return
        last = self.last_change.get(name)
        prev = data.get("prev_change_id")
        if last is not None and prev == last:
            self.last_change[name] = data["change_id"]
            return
        self.counts["gaps"] += 1
        self.elog(
            "gap", instrument=name, expected_prev=last, got_prev=prev, change_id=data["change_id"]
        )
        self.raw.event("gap", instrument=name, expected_prev=last, got_prev=prev)
        self.last_change.pop(name, None)
        self.resyncing.add(name)
        self.resync_q.put_nowait(name)

    async def _resync_worker(self) -> None:
        """Batches gap resubscribes so a burst of gaps can't exceed the subscribe rate limit."""
        while True:
            names = {await self.resync_q.get()}
            await asyncio.sleep(0.2)
            while not self.resync_q.empty():
                names.add(self.resync_q.get_nowait())
            chans = [f"book.{n}.100ms" for n in sorted(names)]
            for part in chunks(chans, SUB_CHUNK):
                await self.call("public/unsubscribe", {"channels": part})
                await asyncio.sleep(SUB_PAUSE_S)
                await self.call("public/subscribe", {"channels": part})
                await asyncio.sleep(SUB_PAUSE_S)
            self.elog("resubscribe", reason="gap", instruments=sorted(names))
            self.raw.event("resubscribe", reason="gap", instruments=sorted(names))

    # ---------- universe ----------

    async def _refresh_universe(self) -> tuple[list[str], list[str]]:
        """Fetch instruments + index, update the sticky option set. Returns (added, removed)."""
        recv_ns, raw, instruments, _ = await self.call(
            "public/get_instruments", {"currency": "BTC", "kind": "option", "expired": False}
        )
        self.raw.write(recv_ns, raw)  # replay needs tick sizes, contract sizes, expiries
        recv_ns, raw, _, _ = await self.call("public/get_instrument", {"instrument_name": PERP})
        self.raw.write(recv_ns, raw)
        _, _, idx, _ = await self.call("public/get_index_price", {"index_name": INDEX_NAME})
        index = float(idx["index_price"])

        now_ms = time.time_ns() // 1_000_000
        by_name = {i["instrument_name"]: i for i in instruments}
        selected = select_universe(
            instruments, index, now_ms, self.args.expiries, self.args.band, self.args.max_strikes
        )
        old = set(self.options)
        expired = {
            n
            for n, i in self.options.items()
            if i["expiration_timestamp"] <= now_ms or n not in by_name
        }
        keep = (old - expired) | selected
        self.options = {n: by_name.get(n, self.options.get(n)) for n in keep}
        added = sorted(keep - old)
        removed = sorted(old - keep)
        expiries = sorted({i["expiration_timestamp"] for i in self.options.values()})
        self.elog(
            "universe",
            index=index,
            n_options=len(self.options),
            expiries=[utc(e * 1_000_000).strftime("%Y-%m-%d %H:%M") for e in expiries],
            added=added,
            removed=removed,
            instruments=sorted(self.options) + [PERP],
        )
        self.raw.event(
            "universe",
            index=index,
            added=added,
            removed=removed,
            instruments=sorted(self.options) + [PERP],
        )
        return added, removed

    async def _subscribe(self, instruments: list[str], extra: list[str] = ()) -> None:
        chans = [c for n in instruments for c in channels_for(n)] + list(extra)
        for part in chunks(chans, SUB_CHUNK):
            _, _, ok, _ = await self.call("public/subscribe", {"channels": part})
            missing = sorted(set(part) - set(ok))
            if missing:
                self.elog("subscribe_missing", channels=missing)
            await asyncio.sleep(SUB_PAUSE_S)
        self.elog("subscribe", instruments=len(instruments), channels=len(chans))

    async def _unsubscribe(self, instruments: list[str]) -> None:
        chans = [c for n in instruments for c in channels_for(n)]
        for part in chunks(chans, SUB_CHUNK):
            await self.call("public/unsubscribe", {"channels": part})
            await asyncio.sleep(SUB_PAUSE_S)
        for n in instruments:
            self.last_change.pop(n, None)
            self.resyncing.discard(n)
        self.elog("unsubscribe", instruments=instruments)

    async def _refresh_loop(self) -> None:
        while True:
            now = time.time()
            wait = 3600 - (now % 3600) + REFRESH_OFFSET_S
            await asyncio.sleep(wait if wait <= 3600 else wait - 3600)
            added, removed = await self._refresh_universe()
            if removed:
                await self._unsubscribe(removed)
            if added:
                await self._subscribe(added)

    # ---------- probe + flush ----------

    async def _probe(self) -> None:
        sent_ns = time.time_ns()
        try:
            _, _, _, rtt_ns = await self.call("public/test", {})
        except TimeoutError:
            self.elog(
                "probe_timeout",
                sent_ts=utc(sent_ns).isoformat(timespec="milliseconds"),
                timeout_s=RPC_TIMEOUT_S,
            )
            return
        rtt = rtt_ns / 1e6
        self.rtts.append(rtt)
        self.min_rtt = rtt if self.min_rtt is None else min(self.min_rtt, rtt)
        self.elog(
            "probe",
            sent_ts=utc(sent_ns).isoformat(timespec="milliseconds"),
            rtt_ms=round(rtt, 3),
            min_rtt_ms=round(self.min_rtt, 3),
            median_rtt_ms_1h=round(statistics.median(self.rtts), 3),
        )

    async def _probe_loop(self) -> None:
        """Probes on a fixed schedule; each round trip runs in its own task so it can't delay
        the next one."""
        async with asyncio.TaskGroup() as probes:
            next_t = time.monotonic()
            while True:
                probes.create_task(self._probe())
                next_t += PROBE_S
                now = time.monotonic()
                if next_t < now:  # event loop stalled: skip missed slots instead of bursting
                    next_t = now
                await asyncio.sleep(next_t - now)

    async def _flush_loop(self) -> None:
        """Keeps flushing through errors (e.g. disk full) so recording resumes if space frees up."""
        while True:
            await asyncio.sleep(FLUSH_S)
            try:
                self.raw.flush()
            except Exception:
                log.exception("flush failed")
                self._safe(self.elog, "flush_error")

    # ---------- connection lifecycle ----------

    async def _session(self) -> None:
        async with connect(self.url, max_size=2**25, open_timeout=15) as ws:
            self.ws = ws
            self.last_change.clear()
            self.resyncing.clear()
            self.elog("connect", url=self.url)
            self.raw.event("connect")
            async with asyncio.TaskGroup() as tg:
                tg.create_task(self._reader())
                await self.call("public/set_heartbeat", {"interval": HEARTBEAT_S})
                await self._refresh_universe()
                await self._subscribe(sorted(self.options) + [PERP], [INDEX_CHANNEL])
                tg.create_task(self._probe_loop())
                tg.create_task(self._refresh_loop())
                tg.create_task(self._resync_worker())

    @staticmethod
    def _safe(fn, *args, **kwargs) -> None:
        """Bookkeeping on the reconnect path must never stop the loop (e.g. log write on a full
        disk); failures go to stderr."""
        try:
            fn(*args, **kwargs)
        except Exception:
            log.exception("bookkeeping failed: %s", getattr(fn, "__name__", fn))

    async def run(self) -> None:
        self._safe(self.elog, "start", args=vars(self.args))
        self.flusher = asyncio.create_task(self._flush_loop())
        attempt = 0
        try:
            while True:
                started = time.monotonic()
                reasons = ["session ended"]
                try:
                    await self._session()
                except* Exception as eg:  # log and reconnect; a week-long run must not die
                    reasons = [f"{type(e).__name__}: {e}" for e in eg.exceptions]
                connected_s = round(time.monotonic() - started, 1)
                self._safe(self.elog, "disconnect", reasons=reasons, connected_s=connected_s)
                self._safe(self.raw.event, "disconnect")
                for fut in self.pending.values():
                    fut.cancel()
                self.pending.clear()
                while not self.resync_q.empty():
                    self.resync_q.get_nowait()
                attempt = 0 if connected_s >= STABLE_S else attempt + 1
                delay = min(BACKOFF_MAX_S, 2 ** max(attempt - 1, 0)) * random.uniform(0.5, 1.0)
                self._safe(self.elog, "reconnect", attempt=attempt, delay_s=round(delay, 2))
                await asyncio.sleep(delay)
        finally:
            self.flusher.cancel()

    def close(self) -> None:
        self.elog("stop", messages=self.counts["messages"], gaps=self.counts["gaps"])
        self.raw.close()
        self.elog.close()


async def main_async(args: argparse.Namespace) -> None:
    rec = Recorder(args, Path(os.environ.get("DATA_DIR", "data")))
    task = asyncio.create_task(rec.run())
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, task.cancel)
    if args.minutes:
        loop.call_later(args.minutes * 60, task.cancel)
    try:
        await task
    except asyncio.CancelledError:
        pass
    finally:
        rec.close()


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "--url", default=os.environ.get("DERIBIT_WS_URL", "wss://www.deribit.com/ws/api/v2")
    )
    p.add_argument("--expiries", type=int, default=3, help="nearest expiries to record")
    p.add_argument(
        "--band", type=float, default=0.15, help="strike band around the index, as a fraction"
    )
    p.add_argument(
        "--max-strikes", type=int, default=20, help="strikes per expiry, closest to the index"
    )
    p.add_argument(
        "--minutes",
        type=float,
        default=0,
        help="stop after this many minutes (0 = run until killed)",
    )
    args = p.parse_args()
    logging.basicConfig(
        level=os.environ.get("LOG_LEVEL", "INFO"),
        format="%(asctime)s %(levelname)s %(message)s",
    )
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
