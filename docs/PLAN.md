# PLAN: milestones

Six milestones. The 7-day recording starts at the end of M1, so the data is ready by about M5 (two-week target).

Each milestone is built from its checklist, tested and reviewed against its "done when", and then committed as a checkpoint.

## M1: Repo skeleton + recorder (start recording)
- [x] CMake project with a C++20 target and doctest wired in (no tests yet)
- [x] `uv` Python project with Ruff and pytest config, `websockets` added
- [x] `recorder/record.py`: picks the universe, subscribes to channels, sets heartbeat, reconnects with backoff, detects gaps, adds local receive time, writes hourly gzipped files to `data/raw/`
- [x] Latency probe (`public/test` round trip every 5 s) in the recorder log
- [x] Hourly instrument refresh (new expiries in, expired out)
- [x] `recorder/check_book.py`: rebuilds books from raw files and compares with a REST snapshot
- [x] `config/fees.json`: fee schedule with the date checked
- [ ] Start the 7-day recording (with `caffeinate` so the Mac doesn't sleep); note disk use per hour
  - Disk use measured in the 65-min check run on 2026-09-27: about 25 MB per full hour (≈0.6 GB/day, ≈4.2 GB for 7 days) for 120 options + BTC-PERPETUAL.

**Done when:** the recorder has run for 1 hour, `check_book.py` matches REST snapshots in at least 99% of checks, latency probes appear in the log, and the long recording is running.

## M2: C++ core: events, books, clock
- [ ] Event types (book snapshot/change, trade, ticker, index, SABR fit)
- [ ] JSON-lines reader on stdin (nlohmann/json), plus a second time-merged input for the fit cache
- [ ] Order book with snapshot + changes, gap detection, stale flag
- [ ] Event loop with exchange-time clock and a latency queue for both directions
- [ ] Top-of-book dump for comparison; time one full recorded day

**Done when:** replaying one recorded hour, the C++ top of book matches `check_book.py` at every event, two runs produce identical output, no event is visible before `time + latency`, and the time for one day is written down.

## M3: Pricing + SABR reuse
- [ ] In the sabr repo: make it an installable package, add optional weights to `calibrate()`, move matplotlib/requests to an extra.
- [ ] Add the tagged sabr repo as a pinned git dependency
- [ ] `sabr_sidecar/fit.py`: per-expiry fits from recorded ticker data with warm start, stamped with compute time; builds `cache/sabr/DAY.jsonl.gz`
- [ ] C++: Black-76 price + Greeks, IV solver, Hagan formula port
- [ ] Report: SABR vs mark IV error for β = 0.5 and 1.0; pick β

**Done when:** pricing meets the spec tolerances, C++ Hagan matches Python `hagan_lognormal_vol` on a grid, the sabr repo's own tests still pass, one recorded day has a fit cache, and the β comparison is written down.

## M4: Exchange simulator, accounts, first two strategies
- [ ] Our orders: place / cancel / replace with latency
- [ ] Optimistic and queue-aware fill models
- [ ] Fees (maker/taker, options vs perpetual), expiry settlement with delivery fee
- [ ] Account object (cash, positions, Greeks, PnL parts, markouts); several accounts per run
- [ ] Strategy interface + registry (written so custom strategies can plug in later)
- [ ] Fixed-spread and mark-IV follower strategies; shared risk limits and quote pulling
- [ ] Run outputs: `config.json`, per-account `fills.csv` / `pnl.csv` / `summary.json`

**Done when:** the fill models behave as the spec says in all listed scenarios, the accounting identity holds at every step for every account, and one recorded day runs end to end with both strategies side by side.

## M5: Remaining strategies, hedgers, results
- [ ] Inventory-skewed (A-S) and join-the-best strategies
- [ ] Hedger interface + no hedge, fixed band, time-based, Whalley–Wilmott (on BTC-PERPETUAL)
- [ ] `analysis/run_grid.py`: runs fill model × latency sweep in parallel across cores, 16 accounts per run
- [ ] `analysis/results.py`: builds the strategy table and the hedger table, each row with its conditions

**Done when:** each strategy and hedger follows its core rule in scenario checks, the speed target holds, the full grid runs on the 7-day dataset, and both tables are produced with complete conditions on every row.

## M6: Local dashboard (Live, Replay, Strategies, Results)
- [ ] `live/feed.py`: public websocket → engine stdin (same format as recorded files), with the SABR sidecar fitting live and feeding the same pipe
- [ ] Engine state output (per-account quotes, fills, positions, Greeks, PnL) as JSON lines on stdout
- [ ] `dashboard/server.py`: stdlib HTTP for static files + websockets to push state; starts and stops engine sessions from the Strategies tab
- [ ] Browser page with uPlot (vendored): Live, Replay (day picker + 1x–100x speed), Strategies (pick strategies, parameters and hedgers), Results tabs
- [ ] README: results, known limits, how to run everything locally, credit for the sabr repo

**Done when:** a session with a mix of strategies and hedgers starts from the Strategies tab, live mode runs 1 hour with no crash and flat memory, replay plays a recorded day at 1x–100x, and a demo video has been recorded.
