"""Local dashboard: serves the page, pushes engine state to it, starts and stops sessions.

  uv run --env-file .env python dashboard/server.py
  then open http://127.0.0.1:8765

Everything is local and paper only. Two listeners, both bound to 127.0.0.1:
  DASHBOARD_PORT      (8765)  stdlib HTTP: the static page and three read-only JSON endpoints
  DASHBOARD_PORT + 1  (8766)  websocket: engine state out, commands in (start / stop / speed)

A session is one engine process fed by one of:
  live    live/feed.py    public Deribit websocket (no account, no key), with live SABR fits.
                          A strategy sees each event when it arrives; its orders are delayed by
                          the lowest one-way latency measured during the 7-day recording.
  replay  live/replay.py  a recorded day at 1x-100x, with that day's fit cache. Market data
                          timing and latency are the same as in the batch runs.
Each session writes a normal run folder (config.json, fills.csv, pnl.csv, summary.json) under
runs/live/<start time>/run or runs/replay/<start time>/run. One session at a time.

HTTP endpoints:
  /api/meta      strategies, hedgers and their parameters, replay days, tuned presets
  /api/results   the M5 tables (docs/results/*.csv)
  /api/curves    PnL over time for the M5 hedger run, if its run folder is still on disk
"""

from __future__ import annotations

import asyncio
import csv
import json
import os
import signal
import sys
import threading
from collections import deque
from datetime import UTC, datetime
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from websockets.asyncio.server import broadcast, serve

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from analysis.run_grid import BASE_QUOTER, BASE_RUN, measured_latencies  # noqa: E402

HOST = "127.0.0.1"
PORT = int(os.environ.get("DASHBOARD_PORT", "8765"))
WS_PORT = PORT + 1
STATIC = Path(__file__).resolve().parent / "static"
DATA_DIR = Path(os.environ.get("DATA_DIR", "data"))
CACHE_DIR = Path(os.environ.get("CACHE_DIR", "cache"))
RUNS_DIR = Path(os.environ.get("RUNS_DIR", "runs"))
ENGINE = Path("build/release/mm_engine")
BETA = 0.5
MAX_ACCOUNTS = 16
STATE_MS = {"live": 1000, "replay": 5000}  # exchange time between state lines
HISTORY_POINTS = 20_000  # PnL points kept per account for a page that connects later
CURVE_POINTS = 1500

# What the page offers. Defaults mirror the engine's (strategies/*.cpp, hedger.cpp, quoter.hpp).
STRATEGIES = {
    "fixed_spread": {"half_spread_vp": 2.0},
    "mark_iv_follower": {"half_spread_vp": 2.0},
    "inventory_skew": {
        "half_spread_vp": 2.0,
        "skew_vp_per_100_vega": 1.0,
        "inventory_widen_vp": 1.0,
        "vol_widen": 0.25,
        "max_vol_widen_vp": 5.0,
    },
    "join_best": {"min_edge_vp": 2.0},
}
HEDGERS = {
    "none": {},
    "fixed_band": {"band_btc": 0.05},
    "time_based": {"interval_min": 15},
    "whalley_wilmott": {"risk_aversion_per_usd": 1e-4, "min_band_btc": 0.005},
}
QUOTER_KNOBS = {"fee_edge_mult": 0, "requote_band_ticks": 0, "reduce_only_before_expiry_s": 3600}


def replay_days() -> list[str]:
    """Days that can be replayed: a recording, a 00:00 book checkpoint and a fit cache."""
    days = []
    for d in sorted((DATA_DIR / "raw").glob("*")):
        day = d.name
        if (
            (d / "00.jsonl.gz").exists()
            and (CACHE_DIR / "books" / day / "00.jsonl").exists()
            and (CACHE_DIR / "sabr" / f"beta-{BETA}-recv" / f"{day}.jsonl.gz").exists()
        ):
            days.append(day)
    return days


def presets() -> dict:
    """The parameter sets frozen in M5, to load into the Strategies tab."""
    path = ROOT / "config" / "m5_params.json"
    if not path.exists():
        return {}
    frozen = json.loads(path.read_text())
    out = {}
    for name, s in frozen["strategies"].items():
        out[name] = {"best_pnl": {"params": s["params"], "quoter": s["quoter"]}}
        if s.get("active"):
            a = s["active"]
            out[name]["best_pnl_min_fills"] = {"params": a["params"], "quoter": a["quoter"]}
    return {"strategies": out, "whalley_wilmott": frozen["whalley_wilmott"]}


_latency: list = []


def lowest_latency() -> float | None:
    """Lowest one-way latency (ms) from the recorder's probes; None without recorder logs."""
    if not _latency:
        try:
            _latency.append(round(measured_latencies()[0], 4))
        except SystemExit:
            _latency.append(None)
    return _latency[0]


def meta() -> dict:
    latency = lowest_latency()
    return {
        "ws_port": WS_PORT,
        "strategies": STRATEGIES,
        "hedgers": HEDGERS,
        "quoter_knobs": QUOTER_KNOBS,
        "base_quoter": BASE_QUOTER,
        "fill_models": ["queue_aware", "optimistic"],
        "days": replay_days(),
        "presets": presets(),
        "default_latency_ms": latency,
        "max_accounts": MAX_ACCOUNTS,
        "engine_built": ENGINE.exists(),
    }


def results() -> dict:
    tables = {}
    for name in ("strategy_table", "hedger_table", "tuning_table"):
        path = ROOT / "docs" / "results" / f"{name}.csv"
        if not path.exists():
            continue
        with path.open() as fh:
            rows = list(csv.reader(fh))
        tables[name] = {"columns": rows[0], "rows": rows[1:]}
    return {"tables": tables}


_curves: dict | None = None


def curves() -> dict:
    """PnL (BTC) over time per account of the M5 hedger run, thinned for plotting."""
    global _curves
    if _curves is None:
        out = []
        run = RUNS_DIR / "m5" / "report" / "hedgers"
        for pnl in sorted(run.glob("*/pnl.csv")):
            with pnl.open() as fh:
                rows = [(int(r["time_ns"]) / 1e9, float(r["pnl_btc"])) for r in csv.DictReader(fh)]
            step = max(1, len(rows) // CURVE_POINTS)
            thin = rows[::step] + ([rows[-1]] if rows and (len(rows) - 1) % step else [])
            out.append(
                {"account": pnl.parent.name, "t": [r[0] for r in thin], "pnl": [r[1] for r in thin]}
            )
        _curves = {"run": str(run), "curves": out}
    return _curves


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self) -> None:
        api = {"/api/meta": meta, "/api/results": results, "/api/curves": curves}.get(self.path)
        if api is None:
            super().do_GET()
            return
        body = json.dumps(api()).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args) -> None:  # the terminal is for session messages
        pass


# ---------------------------------------------------------------------------------------------


def build_config(req: dict, mode: str, desc: list[str]) -> dict:
    """Engine run config from a start request. Raises ValueError with a message for the page."""
    accounts = req.get("accounts")
    if not isinstance(accounts, list) or not 1 <= len(accounts) <= MAX_ACCOUNTS:
        raise ValueError(f"pick between 1 and {MAX_ACCOUNTS} accounts")
    fill_model = req.get("fill_model", "queue_aware")
    if fill_model not in ("queue_aware", "optimistic"):
        raise ValueError("fill model must be queue_aware or optimistic")
    out = []
    for i, a in enumerate(accounts):
        if not isinstance(a, dict):
            raise ValueError(f"account {i + 1} must be an object")
        for key in ("params", "hedger_params", "quoter"):
            if not isinstance(a.get(key) or {}, dict):
                raise ValueError(f"account {i + 1}: {key} must be an object")
        strategy, hedger = a.get("strategy"), a.get("hedger", "none")
        if strategy not in STRATEGIES:
            raise ValueError(f"unknown strategy {strategy!r}")
        if hedger not in HEDGERS:
            raise ValueError(f"unknown hedger {hedger!r}")
        params = {
            k: float(v) for k, v in (a.get("params") or {}).items() if k in STRATEGIES[strategy]
        }
        hparams = {
            k: float(v) for k, v in (a.get("hedger_params") or {}).items() if k in HEDGERS[hedger]
        }
        knobs = {k: float(v) for k, v in (a.get("quoter") or {}).items() if k in QUOTER_KNOBS}
        out.append(
            {
                "name": f"{i + 1:02d}-{strategy}-{hedger}",
                "strategy": strategy,
                "params": params,
                "hedger": hedger,
                "hedger_params": hparams,
                "quoter": {**BASE_QUOTER, **knobs},
            }
        )
    return {
        "inputs": desc,
        "fill_model": fill_model,
        **BASE_RUN,
        "quoter": BASE_QUOTER,
        "accounts": out,
    }


class Session:
    def __init__(self, hub: Hub, mode: str, req: dict) -> None:
        self.hub = hub
        self.mode = mode
        self.day = req.get("day")
        self.speed = float(req.get("speed", 1))
        self.dir = RUNS_DIR / mode / datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
        self.status = "starting"
        self.error = ""
        self.hello: dict | None = None
        self.last_state: str | None = None
        self.names: list[str] = []
        self.history: list[deque] = []
        self.fills: deque = deque(maxlen=300)
        self.index: deque = deque(maxlen=HISTORY_POINTS)  # (time, BTC index) per state
        self.procs: list[asyncio.subprocess.Process] = []
        self.engine: asyncio.subprocess.Process | None = None
        self.feeder: asyncio.subprocess.Process | None = None

        if mode == "replay":
            if self.day not in replay_days():
                raise ValueError(
                    f"{self.day!r} cannot be replayed (needs recording, checkpoint, fits)"
                )
            self.speed = min(100.0, max(1.0, self.speed))
            desc = [
                f"replay of {self.day} at {self.speed:g}x",
                f"cache/sabr/beta-{BETA}-recv/{self.day}.jsonl.gz",
            ]
        else:
            desc = ["live public Deribit feed (live/feed.py), SABR fits computed live"]
        self.config = build_config(req, mode, desc)
        latency = req.get("latency_ms")
        self.latency = float(latency) if latency not in (None, "") else None
        if self.latency is not None and not 0 <= self.latency <= 60_000:
            raise ValueError("latency must be between 0 and 60000 ms")

    def info(self) -> dict:
        return {
            "type": "session",
            "mode": self.mode,
            "day": self.day,
            "speed": self.speed,
            "status": self.status,
            "error": self.error,
            "dir": str(self.dir),
            "hello": self.hello,
        }

    async def start(self) -> None:
        # Anything that can refuse the request comes first, before a pipe or a process exists.
        order_latency = None
        if self.mode == "live":
            order_latency = self.latency if self.latency is not None else lowest_latency()
            if order_latency is None:
                raise ValueError(
                    "no recorded latency probes: set the latency (ms) on the Strategies tab"
                )
        self.dir.mkdir(parents=True, exist_ok=True)
        cfg_path = self.dir / "request.json"
        cfg_path.write_text(json.dumps(self.config, indent=2) + "\n")
        engine_cmd = [
            str(ENGINE), "--config", str(cfg_path), "--run-dir", str(self.dir / "run"),
            "--state-interval-ms", str(STATE_MS[self.mode]),
        ]  # fmt: skip
        env = {**os.environ, "DATA_DIR": str(DATA_DIR), "CACHE_DIR": str(CACHE_DIR)}
        pass_fds: tuple[int, ...] = ()
        feed_r, feed_w = os.pipe()
        feed_log = (self.dir / "feed.log").open("w")
        if self.mode == "live":
            engine_cmd += [
                "--md-timing", "exchange", "--md-latency-ms", "0",
                "--order-latency-ms", f"{order_latency:.4f}",
            ]  # fmt: skip
            feeder_cmd = [sys.executable, "live/feed.py", "--session-dir", str(self.dir / "feed")]
        else:
            control = self.dir / "speed"
            control.write_text(f"{self.speed:g}\n")
            feeder_cmd = [
                sys.executable, "live/replay.py", self.day,
                "--speed", f"{self.speed:g}", "--control", str(control),
            ]  # fmt: skip
            if self.latency is not None:
                engine_cmd += [
                    "--md-latency-ms",
                    str(self.latency),
                    "--order-latency-ms",
                    str(self.latency),
                ]
            fits = CACHE_DIR / "sabr" / f"beta-{BETA}-recv" / f"{self.day}.jsonl.gz"
            fit_r, fit_w = os.pipe()
            gunzip = await asyncio.create_subprocess_exec("gunzip", "-c", str(fits), stdout=fit_w)
            os.close(fit_w)
            self.procs.append(gunzip)
            engine_cmd += ["--fits", f"/dev/fd/{fit_r}"]
            pass_fds = (fit_r,)

        self.feeder = await asyncio.create_subprocess_exec(
            *feeder_cmd, stdout=feed_w, stderr=feed_log, env=env, cwd=ROOT
        )
        os.close(feed_w)
        self.engine = await asyncio.create_subprocess_exec(
            *engine_cmd, stdin=feed_r, stdout=asyncio.subprocess.PIPE,
            stderr=(self.dir / "engine.log").open("w"), env=env, cwd=ROOT,
            pass_fds=pass_fds, limit=2**26,
        )  # fmt: skip
        os.close(feed_r)
        for fd in pass_fds:
            os.close(fd)
        self.procs += [self.feeder, self.engine]
        self.status = "running"
        self.hub.send(self.info())
        asyncio.create_task(self.pump())

    async def pump(self) -> None:
        """Engine stdout -> every connected page; keeps a short history for late joiners."""
        assert self.engine is not None and self.engine.stdout is not None
        async for raw in self.engine.stdout:
            line = raw.decode().rstrip("\n")
            if line.startswith('{"type":"state"'):
                state = json.loads(line)
                if not self.history:
                    self.names = [a["name"] for a in state["accounts"]]
                    self.history = [deque(maxlen=HISTORY_POINTS) for _ in self.names]
                for h, a in zip(self.history, state["accounts"], strict=True):
                    h.append((state["t"] / 1e9, a["pnl"]["total"]))
                self.fills.extend(state["fills"])
                if state.get("index") is not None:
                    self.index.append((state["t"] / 1e9, state["index"]))
                self.last_state = line
            elif line.startswith('{"type":"hello"'):
                self.hello = json.loads(line)
                self.hub.send(self.info())
                continue
            self.hub.send_text(line)
        rc = await self.engine.wait()
        if self.status != "stopping" and rc != 0:
            tail = (self.dir / "engine.log").read_text()[-400:]
            self.error = f"engine exited with code {rc}: {tail.strip()}"
        self.status = "ended"
        await self.cleanup()
        self.hub.send(self.info())

    async def stop(self) -> None:
        if self.status in ("ended", "stopping"):
            return
        self.status = "stopping"
        self.hub.send(self.info())
        # Ending the feed gives the engine end-of-input, so it writes its run folder and exits.
        if self.feeder is not None and self.feeder.returncode is None:
            self.feeder.send_signal(signal.SIGTERM)
        if self.engine is not None:
            try:
                await asyncio.wait_for(self.engine.wait(), timeout=20)
            except TimeoutError:
                self.engine.terminate()

    async def cleanup(self) -> None:
        for p in self.procs:
            if p.returncode is None:
                p.terminate()

    def set_speed(self, speed: float) -> None:
        if self.mode != "replay" or self.status != "running":
            return
        self.speed = min(100.0, max(1.0, float(speed)))
        (self.dir / "speed").write_text(f"{self.speed:g}\n")
        self.hub.send(self.info())

    def catch_up(self) -> list[str]:
        """What a page that connects mid-session needs to draw the same screen."""
        out = [json.dumps(self.info())]
        if self.history:
            series = [{"t": [p[0] for p in h], "pnl": [p[1] for p in h]} for h in self.history]
            index = {"t": [p[0] for p in self.index], "v": [p[1] for p in self.index]}
            out.append(json.dumps({"type": "history", "names": self.names, "series": series,
                                   "fills": list(self.fills), "index": index}))  # fmt: skip
        if self.last_state:
            # Its fills are already in the history above.
            out.append(json.dumps({**json.loads(self.last_state), "fills": []}))
        return out


class Hub:
    def __init__(self) -> None:
        self.clients: set = set()
        self.session: Session | None = None

    def send(self, obj: dict) -> None:
        self.send_text(json.dumps(obj))

    def send_text(self, text: str) -> None:
        broadcast(self.clients, text)

    async def handle(self, ws) -> None:
        self.clients.add(ws)
        try:
            if self.session is not None:
                for msg in self.session.catch_up():
                    await ws.send(msg)
            else:
                await ws.send(json.dumps({"type": "session", "status": "none"}))
            async for raw in ws:
                try:
                    await self.command(json.loads(raw))
                except (ValueError, KeyError, TypeError, AttributeError, OSError) as e:
                    await ws.send(json.dumps({"type": "error", "message": str(e)}))
        finally:
            self.clients.discard(ws)

    async def command(self, msg: dict) -> None:
        cmd = msg.get("cmd")
        if cmd == "start":
            if self.session is not None and self.session.status in (
                "starting",
                "running",
                "stopping",
            ):
                raise ValueError("a session is already running; stop it first")
            if not ENGINE.exists():
                raise ValueError(f"{ENGINE} is missing: build the engine first (see README)")
            mode = msg.get("mode")
            if mode not in ("live", "replay"):
                raise ValueError("mode must be live or replay")
            session = Session(self, mode, msg)
            previous = self.session
            self.session = session
            try:
                await session.start()
            except BaseException:
                # A start that failed must not block the next one.
                self.session = previous
                await session.cleanup()
                raise
        elif cmd == "stop":
            if self.session is not None:
                await self.session.stop()
        elif cmd == "speed":
            if self.session is not None:
                self.session.set_speed(float(msg["value"]))
        else:
            raise ValueError(f"unknown command {cmd!r}")


async def main_async() -> None:
    hub = Hub()
    httpd = ThreadingHTTPServer((HOST, PORT), partial(Handler, directory=str(STATIC)))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    # Only this machine's own page may connect: other sites' scripts are refused by origin.
    origins = [f"http://{h}:{PORT}" for h in ("127.0.0.1", "localhost")]
    stop = asyncio.get_running_loop().create_future()
    for sig in (signal.SIGINT, signal.SIGTERM):
        asyncio.get_running_loop().add_signal_handler(
            sig, lambda: stop.done() or stop.set_result(None)
        )
    async with serve(hub.handle, HOST, WS_PORT, origins=origins, max_size=2**20):
        print(
            f"dashboard: http://{HOST}:{PORT}  (websocket on {WS_PORT}; Ctrl-C to stop)", flush=True
        )
        await stop
        if hub.session is not None:
            await hub.session.stop()
    httpd.shutdown()


def main() -> None:
    os.chdir(ROOT)
    asyncio.run(main_async())


if __name__ == "__main__":
    main()
