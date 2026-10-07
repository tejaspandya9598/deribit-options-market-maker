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
  - Recording stopped by hand on 2026-10-05 at 22:18:03 UTC (35 min past the window; the recorder logged `stop`, 90,689,348 messages, 11 gaps). All of it was copied into `data/` on the HDD and checked file by file with SHA-256 (177 files, 8.2 GB with the earlier recordings). The two `recorder-2026-09-28.jsonl` logs were joined, HDD part first. Recorder log over the window: 41 `connect`, 277 `disconnect` / `reconnect`, 11 `gap`, 4 `heartbeat_miss`, 26 `probe_timeout`.

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
- [x] Our orders: place / cancel / replace with latency
- [x] Optimistic and queue-aware fill models
- [x] Fees (maker/taker, options vs perpetual), expiry settlement with delivery fee
- [x] Account object (cash, positions, Greeks, PnL parts, markouts); several accounts per run
- [x] Strategy interface + registry (written so custom strategies can plug in later)
- [x] Fixed-spread and mark-IV follower strategies; shared risk limits and quote pulling
- [x] Run outputs: `config.json`, per-account `fills.csv` / `pnl.csv` / `summary.json`
  - Run it: `cat cache/books/DAY/00.jsonl <(gunzip -c data/raw/DAY/*.jsonl.gz) | build/release/mm_engine --config config/run_m4.json --run-dir runs/m4/<run_id> --fits <(gunzip -c cache/sabr/beta-0.5-recv/DAY.jsonl.gz)` (fit cache first: `uv run --env-file .env python sabr_sidecar/fit.py DAY --beta 0.5 --clock recv`). The optimized build is in `build/release` (`build/` itself is a Debug build).
  - **Trade-through fills are a setting**, `trade_through`: `trade_size` (default) or `full`, applied to both fill models. The first spec wording (queue-aware fills fully on a trade-through, optimistic only up to the trade's size) broke acceptance 5: a 1.0 bid and a 0.1 trade through it gave 1.0 vs 0.1. `trade_size` is the default because the aggressor only traded that much. Every results row names the setting.
  - **Other rules added while building** (all in the code comments and SPEC 6): quotes are post-only and rejected if they would cross; a replace loses its queue place unless the price is unchanged and the size no larger; maker fills are rounded down to whole lots (0.1); after a book gap an order restarts at the back of the queue; an order is only filled by trades stamped inside its own lifetime, so a late trade print cannot fill an order placed after it and can still fill one cancelled after it.
  - **PnL parts:** spread (fill vs mid at the fill), fees, adverse selection (mid move in the first 60 s after each fill), option MTM (mid move after that, plus settlement), hedge (perpetual). They are added up separately from cash + positions; `summary.json` prints the difference as `accounting_residual_btc`.
  - **`pnl.csv` cadence: every 10 s**, plus a row at every fill and settlement; interval rows are skipped while an account is flat and unchanged. Measured: 8,299 rows and 2.1 MB per account per day. The M5 grid (80 account-runs × 7 days) comes to about 1.2 GB at 10 s, against about 12 GB at 1 s and 0.2 GB at 60 s. PnL accuracy does not depend on it: marks, markouts and settlement are computed at their own times, not at row times.
  - **Late events, measured** (the M2 question), 2026-09-29: 198,632 of 210,101 trades arrive behind the clock; 34% of those by at most 100 ms, 79% by at most 250 ms, 99.65% by at most 1 s, the worst 4.6 s. Hence `late_trade_window_ms` = 1000.
  - **Market-data timing: `--md-timing received` is the default** (decided 2026-10-06). The public feed is slower than the simulated latency: receive time minus exchange timestamp, one hour (2026-09-29 12 UTC), median / p90: option tickers 643 / 1,173 ms, option books 280 / 546 ms, trades 239 / 372 ms, perpetual book 61 / 106 ms. Showing a strategy each event at exchange time + 37 ms is a best case for a feed with no delay, not for the recorded one.
    - `received`: a strategy sees each event when the recorder got it. The recorded receive time already holds our network latency (lowest probe round trip / 2), which is swapped for `--md-latency-ms`, so the latency sweep still works: `max(exchange time, recv − recorded latency) + md latency`.
    - `exchange`: the old rule, kept for comparison.
    - SABR fits get the same treatment: `sabr_sidecar/fit.py --clock recv` only uses tickers received by the fit time and writes `cache/sabr/beta-<β>-recv/`. The engine refuses an exchange-clock cache in `received` mode. The M3 cache and β report are unchanged.
    - Assumes the recorder's clock and Deribit's are in step (the smallest receive-minus-timestamp gap seen is 43–51 ms on every channel, against 37 ms measured one way).
    - Fills, queues and accounting are untouched: they run on exchange timestamps in both modes.
  - **One day, both strategies side by side** — a loss in all eight cases, after fees. Conditions: BTC options, 2026-09-28 23:59:54 → 2026-09-29 23:59:59 UTC, 92 options quoted (|delta| 0.10–0.90, the three recorded expiries, none in the last hour before expiry), 0.1 contract per quote, half-spread 2.0 vol points, max 1.0 contract per option and 100 USD/vol-point vega per account, no hedger, latency 37.5 ms each way (lowest probe round trip 74.9 ms / 2), `trade_through` = `trade_size`, fees as checked 2026-09-27 (standard tier), SABR β = 0.5 (fit cache on the matching clock), 7 expiring options settled at Deribit's delivery price, starting equity 1 BTC. USD at the last index (83,632). Run folders: `runs/m4/2026-09-29_<fill model>_<timing>`.

    | Timing | Fill model | Strategy (fair value) | Fills | PnL BTC | PnL USD | Spread | Fees | Adverse 60 s | Option MTM |
    |---|---|---|---|---|---|---|---|---|---|
    | received | queue-aware | fixed_spread (SABR) | 17 | −0.000859 | −71.82 | +0.000360 | −0.000431 | +0.000025 | −0.000813 |
    | received | queue-aware | mark_iv_follower (mark IV) | 23 | −0.002637 | −220.53 | +0.000465 | −0.000421 | −0.000400 | −0.002281 |
    | received | optimistic | fixed_spread (SABR) | 39 | −0.000673 | −56.25 | +0.000895 | −0.000920 | −0.000040 | −0.000608 |
    | received | optimistic | mark_iv_follower (mark IV) | 50 | −0.005309 | −444.03 | +0.001190 | −0.001068 | −0.000385 | −0.005047 |
    | exchange | queue-aware | fixed_spread (SABR) | 15 | −0.000555 | −46.42 | +0.000285 | −0.000383 | +0.000030 | −0.000488 |
    | exchange | queue-aware | mark_iv_follower (mark IV) | 23 | −0.002637 | −220.53 | +0.000465 | −0.000421 | −0.000400 | −0.002281 |
    | exchange | optimistic | fixed_spread (SABR) | 36 | −0.002081 | −174.04 | +0.000765 | −0.000796 | −0.000055 | −0.001995 |
    | exchange | optimistic | mark_iv_follower (mark IV) | 49 | −0.005329 | −445.71 | +0.001165 | −0.001038 | −0.000410 | −0.005047 |

    The queue-aware mark_iv_follower row is the same in both timings: the same 23 trades filled it at the same prices (order ids differ).
  - **Why they lose** (received mode, same conditions):
    - *Fees eat the spread.* Per contract, edge at the fill is 0.00020–0.00024 BTC and the fee 0.00018–0.00025 BTC; fees come to 90–120% of the spread earned, and after the 60 s adverse move all four accounts are below zero before any inventory drift. Deribit charges 0.0003 BTC per contract (capped at 12.5% of the premium) to makers too, which is about 7% of the average premium traded here (0.0024–0.0037 BTC).
    - *Unhedged inventory is the largest loss.* For mark_iv_follower the perpetual-hedgeable part (delta × index move, summed over the 10 s rows) is −0.0031 of a −0.0027 BTC inventory loss (queue-aware) and −0.0059 of −0.0053 (optimistic). fixed_spread's delta part is small (−0.0001 / +0.0003); its loss is short gamma: on options expiring that day (net short 0.3 calls) it took in 0.0006 BTC of premium and paid 0.0030 at settlement after the index rose 0.9% into expiry (queue-aware).
    - *Inventory piles up on one side.* mark_iv_follower: 14 of 23 fills are buys, 0.6 contract long in one put; nothing leans the quotes against the position.
    - *Every re-quote goes to the back of the queue:* 315,000–404,000 replaces per account that day.
    - *The sample is tiny:* 17–50 fills. Removing the delta part (and ignoring hedge costs) gives +0.0004 / +0.0006 BTC for mark_iv_follower and −0.0008 / −0.0009 for fixed_spread: around zero either way.
  - Deribit's order rate limits are not modelled.
  - **Review fixes (2026-10-06), both applied; the table above is from the re-run:**
    - A maker fill's mid is now the mid at the trade's own timestamp, not the book as it stood when a late print arrived. It comes from two short histories per instrument, book mids and ticker mark prices, each on its own channel's timestamps, because book and ticker messages interleave out of order (second review round: a single merged history got that case wrong; switching to two changed no fill on this day). Stamps also go backwards inside one channel (third round: 1,594 ticker and 22 book cases in one hour, 194 with a changed mark), so the rule is: the message that arrived last is the current state, in effect from its own stamp, and it replaces history entries stamped at or after it. Open assumption: Deribit does not document the ticker timestamp, so whether a later-arriving, older-stamped ticker is newer or stale data is unverified; the engine treats it as newer everywhere (market state, strategy view, this history). Same fills, totals, fees and option MTM as before; 3–10 fills per account got a different mid, which moved PnL from the adverse part to spread (by 0.000015–0.000205 BTC).
    - In `received` mode a fit is never delivered before `fit_time + compute`; it is only delayed, by `md latency − recorded latency` when that is positive. Before, a latency setting below the recorded one delivered fits early. No effect on these runs (both latencies are 37.5 ms).
  - Speed: the day replays in 117.8 s with 2 accounts (optimized build, M2 Air, exchange timing, one run at a time), against 120.0 s with none in M2. In received mode three runs side by side took 125.7 s each. The 16-account target is checked in M5.

**Done when:** the fill models behave as the spec says in all listed scenarios, the accounting identity holds at every step for every account, and one recorded day runs end to end with both strategies side by side.

## M5: Remaining strategies, hedgers, results
- [x] Inventory-skewed (A-S) and join-the-best strategies
- [x] Hedger interface + no hedge, fixed band, time-based, Whalley–Wilmott (on BTC-PERPETUAL)
- [x] `analysis/run_grid.py`: runs fill model × latency sweep in parallel across cores, 16 accounts per run
- [x] `analysis/results.py`: builds the strategy table and the hedger table, each row with its conditions
- From the M4 results (agreed 2026-10-06), as quoter settings shared by every strategy:
  - [x] Fee-aware quotes: never quote closer to fair than the fee plus a set margin, whatever the vol-point spread says (`fee_edge_mult`: the distance is that multiple of the maker fee)
  - [x] Re-quote band: leave a resting quote alone while it is within N ticks of the wanted price, to keep its queue place (`requote_band_ticks`)
  - [x] Near expiry: stop opening positions earlier than the last hour and only quote the side that reduces the position (`reduce_only_before_expiry_s`)
- [x] Build the receive-clock fit cache (`fit.py --clock recv`) for all 7 days; the grid needs it
- [x] Tune on 2 days, report on 5 (agreed 2026-10-06). Tuning window: 2026-09-28 21:42:41 → 2026-09-30 21:42:41 UTC. Report window: 2026-09-30 21:42:41 → 2026-10-05 21:42:41 UTC. Every parameter is fixed before the report window is run; the results tables use the report window only, and say so. A tuning-window table is kept next to them, labelled as in-sample.
  - Run it: `uv run --env-file .env python analysis/run_grid.py tune`, then `... run_grid.py report`, then `uv run --env-file .env python analysis/results.py`. Tables land in `runs/m5/results/` (`strategy_table`, `hedger_table`, `tuning_table`, each as `.csv` with every condition per row and as `.md`). `runs/` is not tracked, so a copy of the six files as of the M5 commit is in [docs/results/](results/). Run folders: `runs/m5/tune/`, `runs/m5/report/`.
  - **Tuning** (in-sample, not a result). 140 parameter sets (36 / 36 / 32 / 36 per strategy), 16 accounts per engine run, queue-aware fills, lowest latency, Whalley–Wilmott hedger; then 4 risk-aversion values on the picked sets. Objective: total PnL in BTC after fees. Frozen in [config/m5_params.json](../config/m5_params.json):

    | Strategy | Picked set (best PnL) | In-sample PnL BTC | In-sample fills |
    |---|---|---|---|
    | fixed_spread | half-spread 6 vp, no fee floor, no band | −0.000179 | 1 |
    | mark_iv_follower | half-spread 2 vp, fee floor 2×, no band | +0.000931 | 21 |
    | inventory_skew | half-spread 2 vp, skew 2 vp per 100 vega, fee floor 1×, band 1 tick | −0.000751 | 41 |
    | join_best | min edge 4 vp, fee floor 2×, no band | 0 | 0 |

    Whalley–Wilmott risk aversion: 1e-4 per USD (band floor 0.005 BTC). The near-expiry window stayed at 1 hour in every pick, i.e. off. Not tuned: quote size 0.1, max 1.0 contract per option, vega limit 100 USD per vol point, fixed band 0.05 BTC, time-based 15 min.
  - **The objective picked sets that hardly trade** for two strategies: every fixed_spread and join_best set lost money in-sample, so the best was the one that traded least (1 fill and 0 fills). Only 7 of the 140 sets made money, all mark_iv_follower. The agreed rule and its picks were kept. A second pick was added for those two strategies before any report-window run: the best PnL among sets with at least 20 maker fills in the tuning window (10 a day). fixed_spread: half-spread 4 vp, no fee floor, band 1 tick (in-sample −0.002922 BTC, 24 fills). join_best: min edge 2 vp, fee floor 1× (in-sample −0.002315 BTC, 27 fills). The threshold was chosen after seeing the tuning results, and both picks are reported.
  - **Report window, run once with those parameters.** Conditions for every row below: BTC options, 2026-09-30 21:42:41 → 2026-10-05 21:42:41 UTC (index 83,851 → 85,912), 206–240 options quoted per account (|delta| 0.10–0.90, none in the last hour before expiry), 0.1 contract per quote, max 1.0 contract per option, 100 USD/vol-point vega limit, `trade_through` = `trade_size`, market data as received, fees as checked 2026-09-27 (standard tier), SABR β = 0.5 on the receive clock (mark_iv_follower: Deribit mark IV), starting equity 1 BTC, positions still open at the end marked to mid. USD at the last index. All figures after fees.
  - **Hedger table** (queue-aware fills, latency 37.5 ms each way; fixed_spread and join_best use their min-20-fills set, since a set that never trades has nothing to hedge):

    | Strategy | Hedger | Fills | Hedge orders | PnL BTC | PnL USD | Spread | Fees | Adverse 60 s | Option MTM | Hedge | Max drawdown |
    |---|---|---|---|---|---|---|---|---|---|---|---|
    | fixed_spread | none | 61 | 0 | +0.000600 | +51.51 | +0.001940 | −0.001745 | −0.002485 | +0.002890 | +0.000000 | 0.012043 |
    | fixed_spread | fixed_band | 61 | 275 | −0.009508 | −816.82 | +0.001940 | −0.008417 | −0.002485 | +0.002890 | −0.003435 | 0.012546 |
    | fixed_spread | time_based | 61 | 389 | −0.005932 | −509.67 | +0.001940 | −0.005525 | −0.002485 | +0.002890 | −0.002752 | 0.009031 |
    | fixed_spread | whalley_wilmott | 61 | 1230 | −0.008861 | −761.23 | +0.001940 | −0.003202 | −0.002485 | +0.002890 | −0.008004 | 0.012018 |
    | mark_iv_follower | none | 44 | 0 | +0.005327 | +457.62 | +0.000815 | −0.001072 | −0.001535 | +0.007119 | +0.000000 | 0.009868 |
    | mark_iv_follower | fixed_band | 44 | 112 | −0.006081 | −522.41 | +0.000815 | −0.004063 | −0.001535 | +0.007119 | −0.008417 | 0.007009 |
    | mark_iv_follower | time_based | 44 | 394 | −0.003402 | −292.30 | +0.000815 | −0.003333 | −0.001535 | +0.007119 | −0.006468 | 0.004651 |
    | mark_iv_follower | whalley_wilmott | 44 | 572 | −0.001508 | −129.60 | +0.000815 | −0.002111 | −0.001535 | +0.007119 | −0.005797 | 0.003950 |
    | inventory_skew | none | 130 | 0 | +0.014406 | +1237.65 | +0.002995 | −0.003218 | −0.003685 | +0.018314 | +0.000000 | 0.029064 |
    | inventory_skew | fixed_band | 130 | 417 | −0.015105 | −1297.66 | +0.002995 | −0.012281 | −0.003685 | +0.018314 | −0.020447 | 0.016272 |
    | inventory_skew | time_based | 130 | 446 | −0.010774 | −925.61 | +0.002995 | −0.008247 | −0.003685 | +0.018314 | −0.020150 | 0.011646 |
    | inventory_skew | whalley_wilmott | 130 | 1210 | −0.009940 | −854.00 | +0.002995 | −0.004737 | −0.003685 | +0.018314 | −0.022827 | 0.014019 |
    | join_best | none | 68 | 0 | +0.007556 | +649.16 | +0.001975 | −0.001803 | −0.001930 | +0.009314 | +0.000000 | 0.014402 |
    | join_best | fixed_band | 68 | 128 | −0.005639 | −484.48 | +0.001975 | −0.004509 | −0.001930 | +0.009314 | −0.010489 | 0.006997 |
    | join_best | time_based | 68 | 386 | −0.003852 | −330.89 | +0.001975 | −0.004290 | −0.001930 | +0.009314 | −0.008920 | 0.005005 |
    | join_best | whalley_wilmott | 68 | 1279 | −0.003271 | −280.98 | +0.001975 | −0.002653 | −0.001930 | +0.009314 | −0.009976 | 0.006453 |

    - No hedged account made money: 0 of 12.
    - All four unhedged accounts are up, and delta explains it: delta × index move summed over the 10 s rows is +0.001252 (fixed_spread), +0.006214 (mark_iv_follower), +0.016740 (inventory_skew), +0.007597 (join_best) BTC, against totals of +0.000600, +0.005327, +0.014406, +0.007556. That is a directional gain in a week the index rose 2.5%, not market-making edge.
    - The hedgers do hedge: mean |account delta| is 0.26–0.50 BTC unhedged, 0.012–0.016 with the fixed band, 0.009–0.021 time-based, 0.08–0.18 with Whalley–Wilmott (its tuned band is wide).
    - Hedging is expensive here: the fixed band traded 0.66–2.2 million USD of perpetual per account and paid 0.0027–0.0091 BTC in taker fees, more than the options side earned in spread. Whalley–Wilmott traded 0.21–0.37 million USD.
  - **Strategy table** (Whalley–Wilmott hedger). The rows shown are at the lowest measured latency (37.5 ms); the last column is the range over all 8 latencies (10, 25, 37.5, 44.4 = measured median, 50, 75, 100, 250 ms). All 96 rows are in `runs/m5/results/strategy_table.md`.

    | Strategy | Parameter set | Fill model | Fills | PnL BTC | PnL USD | Spread | Fees | Adverse 60 s | Option MTM | Hedge | PnL BTC over the 8 latencies |
    |---|---|---|---|---|---|---|---|---|---|---|---|
    | fixed_spread | best PnL | queue-aware | 7 | +0.001514 | +130.07 | −0.000340 | −0.000301 | −0.000580 | +0.003451 | −0.000716 | +0.001420 to +0.001514 |
    | fixed_spread | best PnL | optimistic | 18 | −0.001343 | −115.41 | +0.000800 | −0.001085 | −0.000290 | +0.001757 | −0.002524 | −0.001943 to +0.000268 |
    | fixed_spread | best PnL, min 20 fills | queue-aware | 61 | −0.008861 | −761.23 | +0.001940 | −0.003202 | −0.002485 | +0.002890 | −0.008004 | −0.008861 to −0.007268 |
    | fixed_spread | best PnL, min 20 fills | optimistic | 84 | −0.009120 | −783.50 | +0.002735 | −0.004135 | −0.003220 | +0.002327 | −0.006826 | −0.009421 to −0.007848 |
    | mark_iv_follower | best PnL | queue-aware | 44 | −0.001508 | −129.60 | +0.000815 | −0.002111 | −0.001535 | +0.007119 | −0.005797 | −0.001837 to −0.001329 |
    | mark_iv_follower | best PnL | optimistic | 103 | −0.003390 | −291.25 | +0.003555 | −0.004538 | −0.001585 | +0.001348 | −0.002171 | −0.003390 to −0.000133 |
    | inventory_skew | best PnL | queue-aware | 130 | −0.009940 | −854.00 | +0.002995 | −0.004737 | −0.003685 | +0.018314 | −0.022827 | −0.009944 to −0.008486 |
    | inventory_skew | best PnL | optimistic | 210 | −0.017725 | −1522.80 | +0.005740 | −0.007954 | −0.003975 | +0.026688 | −0.038224 | −0.018000 to −0.017248 |
    | join_best | best PnL | queue-aware | 3 | −0.001004 | −86.29 | +0.000350 | −0.000225 | −0.000150 | −0.000897 | −0.000083 | −0.001005 to −0.000665 |
    | join_best | best PnL | optimistic | 26 | −0.002743 | −235.68 | +0.003505 | −0.001686 | −0.000180 | −0.002831 | −0.001552 | −0.004170 to −0.002578 |
    | join_best | best PnL, min 20 fills | queue-aware | 68 | −0.003271 | −280.98 | +0.001975 | −0.002653 | −0.001930 | +0.009314 | −0.009976 | −0.003573 to −0.001539 |
    | join_best | best PnL, min 20 fills | optimistic | 293 | −0.009900 | −850.55 | +0.010925 | −0.011363 | −0.003130 | +0.020881 | −0.027213 | −0.011150 to −0.009233 |

    - 9 of the 96 rows are positive, all the same account (fixed_spread, 6 vp) with 6–7 fills queue-aware (8 rows) or 15 fills optimistic at 250 ms (1 row). Too few fills to mean anything.
    - Out of sample the one in-sample winner did not hold: mark_iv_follower made +0.000931 BTC on the tuning days and lost 0.001508 on the report days.
    - Latency from 10 to 250 ms moves results by a fill or two, with no consistent direction. The feed's own delay (hundreds of ms) is much larger than the network leg being swept.
    - Optimistic fills are 1.4 to 8.7 times the queue-aware count and lose more in all 6 accounts at the lowest latency: more fills at a loss per fill.
  - Speed: 154 s per recorded day with 16 accounts (the hedger run on its own: 768 s for 5 days, gunzip included; optimized build, M2 Air). The SPEC 7.10 target is 300 s.
  - **Review fixes (2026-10-07), all applied.** None changes a result: all 27 runs (10 tuning, 17 report) were run again with the final engine into a scratch folder and match the saved ones in all 831 files. The report window was therefore replayed a second time, with the same frozen parameters and identical output; the tables are unchanged.
    - The time-based hedger no longer trades before its first interval boundary (in the runs its first look was at the window's start with zero delta, so nothing was traded there anyway).
    - A hedge order's limit is rounded to the tick towards the best price, so it never allows more than `hedge_slippage`; before, it could allow up to one tick more.
    - A hedge order whose answer never arrives is given up on after 5 s plus two round trips, instead of blocking further hedges; `summary.json` reports `orders_timed_out` only if it happened.
    - `run_grid.py` reuses a finished run only if the `config.json` the engine wrote records the same settings (fill model, latency, window, every account's parameters) and the run was made by the same engine build (SHA-256 of the binary, kept in `runs/m5/logs/*.engine`). A rebuilt engine runs everything again.
    - Fills of a hedge order that was given up on are counted in the hedging statistics, not only in the position.
  - The first tuning attempt reported all nine runs as failed although they had finished: the engine stops reading at the end of the trading window and the feeding `gunzip` exits on a broken pipe, which the runner counted as a failure. Fixed in the runner (a run is judged by the engine's exit code and by reaching the window's end); the finished runs were reused, not repeated. No report-window run had been made at that point.

**Done when:** each strategy and hedger follows its core rule in scenario checks, the speed target holds, the full grid runs on the 5-day report window with parameters fixed beforehand, and both tables are produced with complete conditions on every row.

## M6: Local dashboard (Live, Replay, Strategies, Results)
- [ ] `live/feed.py`: public websocket → engine stdin (same format as recorded files), with the SABR sidecar fitting live and feeding the same pipe
- [ ] Engine state output (per-account quotes, fills, positions, Greeks, PnL) as JSON lines on stdout
- [ ] `dashboard/server.py`: stdlib HTTP for static files + websockets to push state; starts and stops engine sessions from the Strategies tab
- [ ] Browser page with uPlot (vendored): Live, Replay (day picker + 1x–100x speed), Strategies (pick strategies, parameters and hedgers), Results tabs
- [ ] README: results, known limits, how to run everything locally, credit for the sabr repo

**Done when:** a session with a mix of strategies and hedgers starts from the Strategies tab, live mode runs 1 hour with no crash and flat memory, replay plays a recorded day at 1x–100x, and a demo video has been recorded.
