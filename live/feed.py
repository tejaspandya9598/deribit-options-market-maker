"""Live public Deribit feed -> engine stdin, with SABR fits in the same stream.

Writes to stdout exactly what the recorder writes to its files (one {"recv_ns":..,"msg":..} or
{"recv_ns":..,"event":..} line per message), so the engine reads live data the same way it
reads a recording:

  uv run --env-file .env python live/feed.py --session-dir runs/live/<id>/feed \\
    | build/release/mm_engine --config ... --run-dir ... --md-timing exchange \\
        --md-latency-ms 0 --order-latency-ms <one-way latency> --state-interval-ms 1000

Public data only: no account, no key, no order ever leaves this machine. Connection handling
(universe, subscriptions, heartbeat, reconnect, gap resync, latency probe) is the recorder's own
code (recorder/record.py); only the destination differs. Nothing is written to data/: the
feed's event log (reconnects, gaps, probes) goes to <session-dir>/logs.

SABR fits: every --every seconds of exchange time, each expiry is fitted with the same code as
the fit cache (sabr_sidecar/fit.py, exchange clock: a fit at time t uses the tickers stamped up
to t that have arrived). The fit lines go into the same stream, stamped fit_time + compute time,
so the engine cannot use a fit before it existed.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from recorder.record import Recorder  # noqa: E402
from sabr_sidecar import fit  # noqa: E402


class StdoutWriter:
    """Stands in for the recorder's RawWriter: same calls, lines go to stdout."""

    def __init__(self, on_message) -> None:
        self.out = sys.stdout
        self.on_message = on_message

    def write(self, recv_ns: int, raw: str) -> None:
        if "\n" in raw:
            raw = raw.replace("\n", " ")
        self.out.write(f'{{"recv_ns":{recv_ns},"msg":{raw}}}\n')
        for line in self.on_message(raw):
            self.out.write(line + "\n")
        self.out.flush()

    def event(self, kind: str, **fields) -> None:
        body = json.dumps({"type": kind, **fields}, separators=(",", ":"))
        self.out.write(f'{{"recv_ns":{time.time_ns()},"event":{body}}}\n')
        self.out.flush()

    def flush(self) -> None:
        self.out.flush()

    def close(self) -> None:
        try:
            self.out.flush()
        except BrokenPipeError, ValueError:
            pass  # the reader is gone


class Fitter:
    """The loop of sabr_sidecar.fit.run_day (exchange clock), fed one message at a time."""

    def __init__(self, beta: float, every_s: float) -> None:
        self.beta = beta
        self.every_ns = int(every_s * 10**9)
        self.defs: dict = {}
        self.tickers: dict[str, dict] = {}
        self.warm: dict[int, tuple] = {}
        self.next_fit: int | None = None

    def feed(self, raw: str) -> list[str]:
        """Takes one raw Deribit message; returns the fit lines it triggered (usually none)."""
        if '"result":[{' in raw and '"instrument_name"' in raw:
            fit.load_defs(json.loads(raw), self.defs)
            return []
        if '"channel":"ticker.BTC-' not in raw or "PERPETUAL" in raw:
            return []
        data = json.loads(raw)["params"]["data"]
        ts_ns = data["timestamp"] * 10**6
        out: list[dict] = []
        if self.next_fit is None:
            self.next_fit = (ts_ns // self.every_ns + 1) * self.every_ns
        if ts_ns > self.next_fit:
            fit_at = ((ts_ns - 1) // self.every_ns) * self.every_ns
            fit.fit_all(fit_at, self.tickers, self.defs, self.warm, out, self.beta)
            self.next_fit = fit_at + self.every_ns
        self.tickers[data["instrument_name"]] = {
            "ts_ns": ts_ns,
            "bid_iv": data.get("bid_iv"),
            "ask_iv": data.get("ask_iv"),
            "mark_iv": data.get("mark_iv"),
            "underlying_price": data.get("underlying_price"),
        }
        out.sort(key=lambda f: (f["fit_time_ns"] + f["compute_ns"], f["expiry_ms"]))
        return [json.dumps(f, separators=(",", ":")) for f in out]


class EngineGone(BaseException):
    """The reader of our stdout closed the pipe. Not an Exception, so the recorder's
    reconnect loop (which retries on any Exception) lets it through."""


class LiveFeed(Recorder):
    def __init__(self, args: argparse.Namespace, session_dir: Path) -> None:
        super().__init__(args, session_dir)  # event log -> <session-dir>/logs
        self.fitter = Fitter(args.beta, args.every)
        self.raw = StdoutWriter(self.fitter.feed)

    async def run(self) -> None:
        """The recorder's run loop, except that a closed pipe ends it: reconnecting to Deribit
        cannot bring the engine back."""
        session = self._session

        async def guarded() -> None:
            try:
                await session()
            except BrokenPipeError:
                raise EngineGone from None
            except BaseExceptionGroup as eg:
                if eg.subgroup(BrokenPipeError) is not None:
                    raise EngineGone from None
                raise

        self._session = guarded
        try:
            await super().run()
        except EngineGone:
            pass


async def main_async(args: argparse.Namespace) -> None:
    feed = LiveFeed(args, Path(args.session_dir))
    task = asyncio.create_task(feed.run())
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
        feed.close()


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--session-dir", required=True, help="where the feed's event log goes")
    p.add_argument(
        "--url", default=os.environ.get("DERIBIT_WS_URL", "wss://www.deribit.com/ws/api/v2")
    )
    p.add_argument("--expiries", type=int, default=3, help="nearest expiries to follow")
    p.add_argument("--band", type=float, default=0.15, help="strike band around the index")
    p.add_argument("--max-strikes", type=int, default=20, help="strikes per expiry")
    p.add_argument(
        "--minutes", type=float, default=0, help="stop after this long (0 = until killed)"
    )
    p.add_argument("--beta", type=float, default=0.5, help="SABR beta (0.5 was chosen in M3)")
    p.add_argument("--every", type=float, default=5.0, help="seconds between fits")
    p.add_argument("--min-quotes", type=int, default=4)
    p.add_argument("--min-hours", type=float, default=0.5, help="skip expiries closer than this")
    args = p.parse_args()
    args.clock = "exchange"
    fit.ARGS = args
    logging.basicConfig(
        level=os.environ.get("LOG_LEVEL", "INFO"),
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stderr,
    )
    try:
        asyncio.run(main_async(args))
    except BrokenPipeError:
        pass  # the engine went away; nothing left to feed
    finally:
        # Python flushes stdout once more on exit; with the reader gone that would only print
        # a broken-pipe complaint.
        try:
            sys.stdout.flush()
        except BrokenPipeError, ValueError:
            os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())


if __name__ == "__main__":
    main()
