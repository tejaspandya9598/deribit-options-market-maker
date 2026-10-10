"""Play a recorded day into the engine's stdin at 1x-100x, paced by the recorded receive times.

  uv run --env-file .env python live/replay.py 2026-09-29 --speed 20 --control /tmp/speed \\
    | build/release/mm_engine --config ... --run-dir ... --state-interval-ms 5000 \\
        --fits <(gunzip -c cache/sabr/beta-0.5-recv/2026-09-29.jsonl.gz)

Starts from the day's 00:00 book checkpoint (cache/books/DAY/00.jsonl, written by
`mm_engine --checkpoint-dir`), then sends the day's recorded lines unchanged. A line recorded
t seconds after the first one is sent t / speed seconds after the start. The speed can be
changed while it runs: write a number to the --control file (the dashboard does this).

The engine sees exactly the recorded lines, so its results do not depend on the speed; only
how fast the screen moves does.
"""

from __future__ import annotations

import argparse
import gzip
import os
import sys
import time
import zlib
from pathlib import Path

DATA_DIR = Path(os.environ.get("DATA_DIR", "data"))
CACHE_DIR = Path(os.environ.get("CACHE_DIR", "cache"))
MIN_SPEED, MAX_SPEED = 1.0, 100.0
CONTROL_POLL_S = 0.25


def recv_ns(line: bytes) -> int | None:
    """recv_ns of a recorder line ({"recv_ns":N,...}), without parsing the JSON."""
    if not line.startswith(b'{"recv_ns":'):
        return None
    end = line.find(b",", 11)
    try:
        return int(line[11:end])
    except ValueError:
        return None


def lines(day: str):
    checkpoint = CACHE_DIR / "books" / day / "00.jsonl"
    files = sorted((DATA_DIR / "raw" / day).glob("*.jsonl.gz"))
    if not files:
        sys.exit(f"no recording for {day} in {DATA_DIR / 'raw' / day}")
    if not checkpoint.exists():
        sys.exit(
            f"no book checkpoint {checkpoint} (mm_engine --checkpoint-dir {CACHE_DIR / 'books'})"
        )
    with checkpoint.open("rb") as fh:
        yield from fh
    for path in files:
        with gzip.open(path, "rb") as fh:
            try:
                for line in fh:
                    if line.endswith(b"\n"):
                        yield line
            except EOFError, gzip.BadGzipFile, zlib.error:
                return  # unfinished tail of a file still being written


def clamp(speed: float) -> float:
    return min(MAX_SPEED, max(MIN_SPEED, speed))


def read_speed(control: Path | None, current: float) -> float:
    if control is None:
        return current
    try:
        return clamp(float(control.read_text().strip()))
    except OSError, ValueError:
        return current


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("day", help="YYYY-MM-DD")
    p.add_argument("--speed", type=float, default=1.0, help=f"{MIN_SPEED:g}-{MAX_SPEED:g}x")
    p.add_argument("--control", type=Path, help="file holding the speed; re-read while running")
    args = p.parse_args()

    out = sys.stdout.buffer
    speed = clamp(args.speed)
    first_ns = None  # recorded time and wall time of the current pacing segment
    wall0 = 0.0
    next_poll = 0.0
    try:
        for line in lines(args.day):
            ns = recv_ns(line)
            if ns is not None:
                now = time.monotonic()
                if now >= next_poll:
                    next_poll = now + CONTROL_POLL_S
                    new = read_speed(args.control, speed)
                    if new != speed or first_ns is None:
                        speed, first_ns, wall0 = new, ns, now  # new segment from here
                    out.flush()
                due = wall0 + (ns - first_ns) / 1e9 / speed
                if due > now:
                    out.flush()
                    # Sleep in short steps so a speed change takes effect quickly.
                    while due > now:
                        time.sleep(min(due - now, CONTROL_POLL_S))
                        now = time.monotonic()
                        new = read_speed(args.control, speed)
                        if new != speed:
                            speed, first_ns, wall0 = new, ns, now
                            break
            out.write(line)
        out.flush()
    except BrokenPipeError:
        pass  # the engine went away


if __name__ == "__main__":
    main()
