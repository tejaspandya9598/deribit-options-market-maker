# Deribit BTC options market maker: simulator and paper cockpit

A C++20 event-driven simulator for making markets in BTC options on Deribit. It replays order
books, trades and tickers recorded from Deribit's public API, runs several quoting strategies
side by side with simulated fills, hedges delta on the perpetual, and reports PnL after fees.
The same engine also runs live on the public feed, on paper, behind a local dashboard.

**Paper money only.** There is no account, no API key and no order routing anywhere in this
repository. Every fill is simulated against a recorded or live *public* order book.

## Result

Over the 5 days the strategies were judged on, **none of them made money once delta-hedged**.

- 0 of 12 hedged accounts ended positive.
- All 4 unhedged accounts ended positive, but the gain is directional: the index rose 2.5% that
  week, and delta times the index move accounts for it (for example +0.0167 of
  `inventory_skew`'s +0.0144 BTC).
- 9 of the 96 strategy-table rows are positive, all the same account with 6 to 15 fills.
- The one parameter set that made money on the tuning days (`mark_iv_follower`, +0.000931 BTC)
  lost 0.001508 BTC on the report days.

Where the money goes: Deribit charges makers 0.03% of the underlying per contract (capped at
12.5% of the premium), which is about as much as the quoted spread earns on these short-dated
options; fills are adversely selected over the next minute; and hedging on the perpetual costs
taker fees on every rebalance.

Hedger table: 4 strategies x 4 hedgers, queue-aware fills, 37.5 ms latency each way.

| Strategy | Hedger | Fills | PnL BTC | PnL USD |
|---|---|---|---|---|
| fixed_spread | none | 61 | +0.000600 | +51.51 |
| fixed_spread | fixed_band | 61 | −0.009508 | −816.82 |
| fixed_spread | time_based | 61 | −0.005932 | −509.67 |
| fixed_spread | whalley_wilmott | 61 | −0.008861 | −761.23 |
| mark_iv_follower | none | 44 | +0.005327 | +457.62 |
| mark_iv_follower | fixed_band | 44 | −0.006081 | −522.41 |
| mark_iv_follower | time_based | 44 | −0.003402 | −292.30 |
| mark_iv_follower | whalley_wilmott | 44 | −0.001508 | −129.60 |
| inventory_skew | none | 130 | +0.014406 | +1237.65 |
| inventory_skew | fixed_band | 130 | −0.015105 | −1297.66 |
| inventory_skew | time_based | 130 | −0.010774 | −925.61 |
| inventory_skew | whalley_wilmott | 130 | −0.009940 | −854.00 |
| join_best | none | 68 | +0.007556 | +649.16 |
| join_best | fixed_band | 68 | −0.005639 | −484.48 |
| join_best | time_based | 68 | −0.003852 | −330.89 |
| join_best | whalley_wilmott | 68 | −0.003271 | −280.98 |

Conditions for every row: BTC options on Deribit, 2026-09-30 21:42:41 to 2026-10-05 21:42:41
UTC, 235 to 240 options quoted per account (|delta| 0.10 to 0.90 in the three nearest expiries,
none in the last hour before expiry), 0.1 contract per quote, at most 1.0 contract per option
and 100 USD per vol point of vega per account, market data shown to strategies when the
recorder received it, fees as checked on 2026-09-27 (standard tier, no discount), fair value
from SABR with beta 0.5 (`mark_iv_follower`: Deribit's mark IV), starting equity 1 BTC,
positions still open at the end marked to mid. USD at the index at the end (85,912). After
fees. Parameters were tuned on the two days before (2026-09-28 21:42:41 to 2026-09-30 21:42:41
UTC) and fixed before these days were run.

The full tables, every row with all its conditions, are in [docs/results/](docs/results/):
`strategy_table` (4 strategies x 2 fill models x 8 latencies), `hedger_table`, and
`tuning_table` (every parameter set tried; in-sample, not a result). How the numbers were
produced, and what was found on the way, is in [docs/PLAN.md](docs/PLAN.md).

## Known limits

Read the results with these in mind.

- **No market impact.** Recorded books are replayed as they were. Our quotes never change what
  others do, and our hedge orders do not use up the liquidity they hit.
- **Accounts do not compete.** Strategies run side by side on the same data, and one recorded
  trade can fill several of them.
- **Public-feed timing.** The public 100 ms feed delivers option data well after its exchange
  timestamp (tickers a median 0.6 s later in our recording). Strategies see each event when the
  recorder received it. A participant on a faster feed would see more than these strategies do.
- **Fees are a snapshot.** Deribit's standard tier as of 2026-09-27 (`config/fees.json`). Market
  makers with volume discounts pay less, which matters here because fees are about the size of
  the spread.
- **Fill models are models.** "Queue-aware" puts our order behind everything displayed at its
  price and estimates cancels ahead of it in proportion; "optimistic" ignores the queue. The
  truth is somewhere a real order would have to find out.
- **Small sample.** Seven days of one market, 44 to 130 option fills per account in the report
  window. One week cannot separate a small edge from noise.
- **Ticker ordering is an assumption.** Deribit does not document which of two tickers is newer
  when the later-arriving one carries an older timestamp. The engine takes the later arrival.
- **Clocks.** Receive-time logic treats the recorder's clock and Deribit's as in step.
- **Not modelled:** order rate limits, Deribit's market-maker protection, block trades, combos,
  portfolio margin, hedging with anything but BTC-PERPETUAL.
- **Two parameter sets for two strategies.** Tuning for highest PnL picked sets that barely
  trade for `fixed_spread` and `join_best` (every set lost money, so the least active won). A
  second set, the best among those with at least 20 fills on the tuning days, was added for
  those two before the report days were run. Both are in the strategy table; the hedger table
  above uses the second.

## How it works

```
recorder/record.py ──> data/raw/DAY/HH.jsonl.gz ──┐
                                                  ├─> mm_engine ──> runs/<id>/<account>/
sabr_sidecar/fit.py ──> cache/sabr/…/DAY.jsonl.gz ┘      │              fills.csv, pnl.csv, summary.json
                                                         └─> state lines ──> dashboard
live/feed.py (public websocket + live SABR fits) ──> mm_engine   (same engine, live)
```

- **Engine (C++20, `engine/src/`).** An event loop on exchange timestamps with a latency queue
  in each direction, so a strategy cannot see anything before it could have. Order books are
  rebuilt from snapshots and changes with gap detection. An exchange simulator holds our
  resting orders, fills them against recorded trades under one of two fill models, applies
  fees, and settles options at Deribit's delivery price. Each account tracks cash, positions and
  PnL split into spread, fees, adverse selection (the first 60 s after a fill), option
  mark-to-market and hedge.
- **Pricing.** Black-76 on the forward, an implied-vol solver, and Hagan's SABR formula in C++.
  SABR is calibrated per expiry every 5 s by a Python sidecar that reuses
  [sabr-vol-calibration](https://github.com/tejaspandya9598/sabr-vol-calibration); the fits are
  cached per day and stamped with the time they became available.
- **Strategies.** `fixed_spread` (SABR fair IV ± a spread), `mark_iv_follower` (the same around
  Deribit's mark IV), `inventory_skew` (Avellaneda–Stoikov style: leans against the vega held,
  widens with inventory and realized vol), `join_best` (joins the best bid or ask only when it
  is far enough from fair). Shared rules: risk limits, pulling quotes on stale data and near
  expiry, a fee floor, a re-quote band.
- **Hedgers.** None, fixed delta band, time-based, and Whalley–Wilmott (a no-trade band from
  gamma, cost and risk aversion). All trade BTC-PERPETUAL as takers.
- **Dashboard (`dashboard/`).** A local page with four tabs: Live, Replay (a recorded day at
  1x to 100x), Strategies (pick strategies, parameters and hedgers, then start a session), and
  Results.

What is built and how it is judged: [docs/SPEC.md](docs/SPEC.md).

## Run it

Needs macOS or Linux, CMake, a C++20 compiler, and [uv](https://docs.astral.sh/uv/).
Everything runs locally and costs nothing.

```bash
cp .env.example .env
uv sync                                                  # Python environment
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release   # the engine
cmake --build build/release
```

**Dashboard:**

```bash
uv run --env-file .env python dashboard/server.py
# open http://127.0.0.1:8765
```

Set the accounts on the Strategies tab and press Start there. Live mode reads Deribit's public
websocket and needs no recorded data, with one exception: the delay applied to simulated orders
comes from the recorder's latency probes, so on a machine that has never recorded, enter a
latency (ms) on the Strategies tab first. Half a ping to `www.deribit.com` is a fair start.

The Results tab works from the tables in this repository. Replay and the batch runs need
recorded data, which is not in the repository (about 8 GB for the week):

```bash
# 1. Record (public data; Ctrl-C to stop). Check a recording against REST snapshots.
uv run --env-file .env python recorder/record.py
uv run --env-file .env python recorder/check_book.py --help

# 2. SABR fit cache per recorded day, and hourly book checkpoints over the whole recording
#    (a replay starts from a day's 00:00 checkpoint, so every day but the first can be replayed).
uv run --env-file .env python sabr_sidecar/fit.py 2026-09-29 --beta 0.5 --clock recv
gunzip -c data/raw/*/*.jsonl.gz | build/release/mm_engine \
  --md-latency-ms 0 --order-latency-ms 0 --checkpoint-dir cache/books

# 3. One day through the engine, two strategies side by side.
cat cache/books/2026-09-29/00.jsonl <(gunzip -c data/raw/2026-09-29/*.jsonl.gz) \
  | build/release/mm_engine --config config/run_m4.json --run-dir runs/example \
      --fits <(gunzip -c cache/sabr/beta-0.5-recv/2026-09-29.jsonl.gz)

# 4. The grid behind the tables: tune on 2 days, report on 5, build the tables.
uv run --env-file .env python analysis/run_grid.py tune
uv run --env-file .env python analysis/run_grid.py report
uv run --env-file .env python analysis/results.py
```

The dates and windows in `analysis/run_grid.py` are those of the recording behind the results;
a new recording needs its own. Tests: `ctest --test-dir build` (C++, after a Debug build in
`build/`) and `uv run pytest`.

## Credits

- SABR calibration: [sabr-vol-calibration](https://github.com/tejaspandya9598/sabr-vol-calibration),
  used through `sabr_sidecar/`; its Hagan formula is ported to C++ in `engine/src/pricing.cpp`.
- Vendored, MIT: [nlohmann/json](https://github.com/nlohmann/json),
  [doctest](https://github.com/doctest/doctest), [uPlot](https://github.com/leeoniya/uPlot).
- Python: [websockets](https://github.com/python-websockets/websockets).
- Market data: Deribit's public API. This project is not affiliated with Deribit.
