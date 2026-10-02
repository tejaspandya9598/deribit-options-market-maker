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
- [x] Start the 7-day recording (with `caffeinate` so the Mac doesn't sleep); note disk use per hour
  - Disk use measured in the 65-min check run on 2026-09-27: about 25 MB per full hour (≈0.6 GB/day, ≈4.2 GB for 7 days) for 120 options + BTC-PERPETUAL. Busy daytime hours on 2026-09-28 ran 59–73 MB, so plan on ≈1–1.5 GB/day.
  - Recording hole: the first long run died at 2026-09-28 19:29:00 UTC when the external HDD holding the project was unplugged. No data until 21:42:41 UTC.
  - From 2026-09-28 21:42:41 UTC the recorder runs from the internal disk (copy of record.py in ~/deribit_recorder, data in ~/deribit_data, restart loop). The data goes back into `data/` on the HDD when recording ends.
  - **Headline dataset: 7 continuous days, 2026-09-28 21:42:41 UTC → 2026-10-05 21:42:41 UTC.** Earlier recordings (2026-09-27 20:16 → 2026-09-28 19:29 UTC) are kept as extra data.

**Done when:** the recorder has run for 1 hour, `check_book.py` matches REST snapshots in at least 99% of checks, latency probes appear in the log, and the long recording is running.

## M2: C++ core: events, books, clock
- [x] Event types (book snapshot/change, trade, ticker, index, SABR fit)
- [x] JSON-lines reader on stdin (nlohmann/json), plus a second time-merged input for the fit cache
- [x] Order book with snapshot + changes, gap detection, stale flag
- [x] Event loop with exchange-time clock and a latency queue for both directions
- [x] Top-of-book dump for comparison; time one full recorded day
- [x] Hourly book checkpoints in `cache/books/` so a replay can start at any hour (added during M2)
  - Day timing, 2026-09-29 (15.67M lines, 15.86M events): 120.0 s wall on the M2 Air, optimized build, no accounts, TOB to /dev/null, default latency 37.9 ms (lowest probe RTT 75.8 ms / 2). Leaves ~180 s of the SPEC 7.10 budget for 16 accounts; that target is checked in M4/M5.
  - To check in M4: ~91% of events arrive with an exchange time behind the clock (late events 14.5M of 15.86M on 2026-09-29). Likely 100ms-batching across ~370 channels; measure how late before fills depend on it.

**Done when:** replaying one recorded hour, the C++ top of book matches `check_book.py` at every event, two runs produce identical output, no event is visible before `time + latency`, and the time for one day is written down.

## M3: Pricing + SABR reuse
- [x] In the sabr repo: make it an installable package, add optional weights to `calibrate()`, move matplotlib/requests to an extra.
- [x] Add the tagged sabr repo as a pinned git dependency (v0.2.0 on branch release-0.2; GitHub main untouched)
- [x] `sabr_sidecar/fit.py`: per-expiry fits from recorded ticker data with warm start, stamped with compute time; builds `cache/sabr/beta-<β>/DAY.jsonl.gz`
- [x] C++: Black-76 price + Greeks, IV solver, Hagan formula port
- [x] Report: SABR vs mark IV error for β = 0.5 and 1.0; pick β
  - **β = 0.5 chosen.** 2026-09-29 + 09-30 (recorded BTC options, 3 nearest expiries, ±15% strikes), fits every 5 s per expiry, OTM mid IVs weighted by 1/(bid-ask IV spread), ν ≤ 5/√T. Errors vs Deribit mark IV over the fitted strikes, median over 98,799 fits per β ([runs/sabr/beta_report.json](../runs/sabr/beta_report.json)):

    | Metric (vol points) | β = 0.5 | β = 1.0 |
    |---|---|---|
    | Median strike error | 0.465 | 0.476 |
    | Worst strike error: median / p95 / max | 2.15 / 4.19 / 7.68 | 2.23 / 4.33 / 7.85 |
    | RMSE vs quoted mid | 0.806 | 0.831 |
    | Fits at a ν or ρ bound | 0% | 0% |

    β = 0.5 is better on every error measure, by a small margin. Median fit compute: ≈2 ms (β 0.5) vs ≈3 ms (β 1.0).
  - The first attempt had ν capped at 5 (sabr's default, sized for ~1-year expiries): 99.99% of fits hit it and the median strike error was 1.85 vp. Short-dated smiles need ν on the order of 1/√T, hence `nu_max` = 5/√T.

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
