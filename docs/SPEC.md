# SPEC: Deribit BTC options market maker (simulator + paper cockpit)

## 1. Goal
Build a portfolio project that shows how BTC option market-making strategies would have done on Deribit, both on recorded market data and live on paper.

- A C++20 engine replays recorded Deribit order books and trades. It runs one or more quoting strategies side by side, simulates fills, hedges delta with the BTC perpetual, and reports PnL honestly.
- The same engine also runs live, fed by Deribit's public websocket. It still trades only on paper.
- A local web dashboard lets you pick strategies and hedgers and watch them trade, live or in replay. Its main use is recording a demo video (LinkedIn etc.).
- If a strategy loses money after fees and adverse selection, that is a valid result and we report it as one.

## 2. Rules
- **Paper money only.** No account, no API key, no auth call, no order routing. Every trade is a simulated fill against a recorded or live public order book.
- **Zero cost.** Public endpoints only; nothing hosted; everything runs locally.
- **Few dependencies:** nlohmann/json and doctest (C++, vendored), websockets (Python), uPlot (JS, vendored), and `sabr-vol-calibration` (which brings numpy and scipy).
- **Honesty about numbers.** Every figure carries its conditions: instruments, dates, strategy and parameters, hedger, fill model, latency, fee schedule date, fair-value source. A losing market maker after fees and adverse selection is a valid result and is reported as one.
- **`data/` holds raw recordings only.** Only the recorder writes there.

## 3. Inputs / data sources
All market data comes from Deribit's public websocket API (`wss://www.deribit.com/ws/api/v2`). BTC only.

- **Instrument list:** `public/get_instruments` (BTC options). This gives strike, expiry, tick size and contract size. Refreshed every hour so new expiries get picked up.
- **Universe:** BTC options in the 2–3 nearest expiries, with strikes near the money (about ±15% of the index, capped at ~20 strikes per expiry), plus BTC-PERPETUAL for hedging.
- **Channels recorded:**
  - `book.{instrument}.100ms`: full-depth book, sent as a snapshot followed by changes. Gaps are detected with `change_id` / `prev_change_id`. The tick-by-tick `raw` book needs a login, so we don't use it.
  - `trades.{instrument}.100ms`: public trades.
  - `ticker.{instrument}.100ms`: mark price, mark IV, bid/ask IV, Deribit's Greeks, underlying (forward) and index price.
  - `deribit_price_index.btc_usd`: the index, used for settlement.
- **Latency probe:** every 5 seconds (on a fixed schedule, so the acceptance bound of 10 s holds with margin) the recorder times a round trip of `public/test` on the same connection and logs it. The lowest round trip seen, divided by two, is our best-case one-way latency.
- **Stored format:** the raw Deribit messages, one JSON object per line, gzipped. Files rotate hourly: `data/raw/YYYY-MM-DD/HH.jsonl.gz`. Each line also gets the local receive time in nanoseconds.
- **Recorder log:** reconnects, detected gaps, resubscriptions, heartbeat misses and latency probes go to a separate log. The report quotes these, so data holes are visible.
- **Dataset for headline results:** 7 continuous days of BTC recording, split in two. Parameters are tuned on the first 2 days only; headline results come from the last 5 days, run once with the parameters already fixed. Results from the tuning days are labelled in-sample.
- **Fees:** Deribit's public fee schedule (options and perpetual, maker and taker, delivery fee). The exact numbers and the date we checked them live in `config/fees.json` and are printed in every report.

## 4. Outputs
- **Run folder** `runs/<run_id>/`. Nothing is ever written into `data/` except by the recorder.
  - `config.json`: every setting, including input files, strategies, hedgers and latency.
  - For each account (one strategy + one hedger), in `runs/<run_id>/<account>/`: `fills.csv`, `pnl.csv`, `summary.json`.
  - `pnl.csv` splits PnL into spread earned, fees, option mark-to-market, hedge PnL and adverse selection (markouts). One row every 10 s (a setting), plus one at every fill and settlement; interval rows are skipped while the account is flat and unchanged.
- **SABR fit cache** `cache/sabr/beta-<β>-recv/YYYY-MM-DD.jsonl.gz` (built by `sabr_sidecar/fit.py --clock recv`; without `-recv` and `--clock`, the same cache cut on exchange timestamps, used for the β report and for `--md-timing exchange`): fitted parameters per expiry over time, computed once per recorded day and β and reused by every replay. One JSON object per line, sorted by `fit_time_ns + compute_ns` (the time the engine may use it): `{"fit_time_ns", "compute_ns", "expiry_ms", "beta", "alpha", "rho", "nu", "forward", "rmse_vol"}`, plus diagnostics the engine ignores: `n_quotes`, `t_years`, `mark_err_med_vp`, `mark_err_max_vp` (error vs Deribit mark IV over the fitted strikes, in vol points), `nu_max` (vol-of-vol bound, 5/√T), `at_bound`.
- **Book checkpoints** `cache/books/YYYY-MM-DD/HH.jsonl`: written by `mm_engine --checkpoint-dir cache/books` at each UTC hour. Each holds the latest recorded instrument lists plus one synthetic snapshot per valid book (real `change_id`), so a replay can start at any hour instead of at the recording's first snapshot.
- **Frozen parameters** `config/m5_params.json`: written by `analysis/run_grid.py tune` from the tuning window; the report-window runs read it and nothing else.
- **Results table:** built by a Python script (`analysis/results.py`, into `runs/m5/results/`) from all run summaries. Every row carries its conditions: instrument set, dates, strategy + parameters, hedger, fill model (with its trade-through setting), market-data timing, latency, fee schedule date, fair-value source.
- **Local dashboard** at `http://localhost:<port>`, with four tabs:
  - **Live:** current books, each account's quotes, fills, positions, Greeks (delta, vega, gamma) and PnL.
  - **Replay:** the same screen driven by a recorded day at 1x–100x speed.
  - **Strategies:** pick one or more strategies and set their parameters, pick a hedger for each, pick fill model and latency, then start a session.
  - **Results:** the results table and charts.

## 5. In scope / out of scope
**In scope**
- Python recorder with reconnect, heartbeat, gap detection and latency probe.
- C++ engine: event loop, book rebuild, pricing, strategies, fill simulation, hedging, per-account PnL.
- Pricing in C++: Black-76 prices and Greeks, implied-vol solver, and Hagan's SABR vol formula (for moving the smile with the forward between fits).
- SABR calibration by reusing `sabr-vol-calibration` through a Python sidecar.
- Deribit mark IV as a second fair-value source, compared against SABR.
- 4 built-in strategies: fixed spread, inventory-skewed (Avellaneda–Stoikov), join-the-best with an edge filter, mark-IV follower.
- 4 hedgers: no hedge, fixed delta band, time-based, Whalley–Wilmott band.
- Several strategies at once, each with its own paper account.
- 2 fill models (optimistic, queue-aware), switchable.
- Latency default = lowest measured, plus a sweep.
- Local dashboard with Live, Replay, Strategies and Results tabs.
- Test setup (doctest for C++, pytest for Python) and Ruff lint.

**Out of scope (now)**
- Any real trading, account, API key or authenticated endpoint.
- Hosting of any kind (Pages, cloud, VPS).
- ETH, far expiries, the full option chain.
- Our orders moving the market. Recorded books are replayed as they were, and side-by-side accounts don't see each other's orders. Both are stated as known limits.
- Deribit's market-maker protection, block trades, combos, portfolio margin.
- Hedging with anything other than BTC-PERPETUAL.

**Future features (not planned yet)**
- Custom strategies added by the user (the C++ strategy interface is written with this in mind).
- Machine-learning signals.

## 6. Key methods
**Event loop.** The engine reads JSON lines from stdin, so replay is `gunzip -c ... | engine --fits cache/sabr/DAY.jsonl.gz` and live is `python live/feed.py | engine`. All logic uses exchange timestamps. A strategy never sees an event before `event_time + latency`, and its orders reach the "exchange" after a further `latency`. This rules out look-ahead by design.

**Market-data timing.** The public 100ms feed delivers events well after their exchange timestamp (option tickers a median 0.6 s later in our recording). `--md-timing` sets what a strategy sees:
- `received` (default): each event when the recorder actually got it. The recorded receive time already contains our network latency (lowest probe round trip / 2); that part is replaced by the configured latency, so the sweep still applies: `max(event_time, receive_time − recorded latency) + latency`. Assumes the recorder's clock and the exchange's are in step.
- `exchange`: `event_time + latency`, a feed with no delay of its own. A best case, reported as such.

Fills, queues and accounting run on exchange timestamps in both modes. Every results row names the mode.

**Order book.** The book is rebuilt from the snapshot plus changes. If `prev_change_id` doesn't match, the instrument is marked stale and we stop quoting it until the next snapshot. Quotes are also pulled if an instrument sees no update for N seconds.

**Pricing (C++).** Black-76 on the forward, using Deribit's underlying price for each expiry. Implied volatility is solved with Newton's method, falling back to bisection. Greeks are computed in closed form. Prices are kept in BTC, converted to USD only for reporting.

**Smile fit (reusing sabr-vol-calibration):**
- Small changes in the sabr repo: make it installable as a package (it has `package = false` today), add optional per-strike weights to `calibrate()`, move matplotlib/requests to an optional extra. Then tag a version. This project depends on it as a pinned git dependency.
- A Python sidecar calls `sabr.calibration.calibrate` for each expiry every few seconds. Inputs:
  - out-of-the-money mid IVs from the ticker's bid/ask IV,
  - weights set by 1 / IV spread,
  - the forward from the ticker,
  - a warm start from the previous fit.
- Each fit only uses data up to its own time: tickers received by then (`--clock recv`, for `--md-timing received`) or stamped by then (`--clock exchange`). Its result reaches the engine at `fit_time + measured fit compute time`, never earlier; in `received` mode it is delayed further by `latency − recorded latency` when that is positive, since its inputs would have arrived that much later. The engine refuses an exchange-clock cache in `received` mode.
- Between fits, C++ re-evaluates Hagan's formula (a port of `sabr/model.py`'s `hagan_lognormal_vol`) as the forward moves.
- β is fixed. Both 0.5 and 1.0 are tried on recorded data, and the one with lower fit error is chosen and documented.
- For replay, fits are computed once per day and cached. For live, the sidecar feeds them straight into the engine's input.
- Fit error against Deribit mark IV is reported.

**Strategies.** Each built-in strategy sets bid/ask quotes on a chosen set of near-money options, in small size. All are rounded to the tick.
- *Fixed spread:* SABR fair IV ± a fixed half-spread in vol points.
- *Inventory-skewed (Avellaneda–Stoikov style):* the quote centre moves against our vega inventory (`centre = fair IV − k · vega_position`, the account's vega), and the half-spread widens with inventory (by a set amount at the vega limit) and with recent volatility (when the index's realized vol, an exponentially weighted average, is above fair IV).
- *Join-the-best with edge filter:* quote at the current best bid/ask, each side only when that price is at least X vol points on the right side of SABR fair value. Otherwise stay out on that side.
- *Mark-IV follower:* fixed spread centred on Deribit mark IV instead of SABR, which turns the fair-value comparison into a strategy choice.
- *Shared risk limits:* max contracts per option and max total vega. Quotes are pulled on stale data and in the last hour before expiry.
- *Shared quote rules* (settings, the same for every strategy; added after the M4 results):
  - *fee floor:* no quote closer to the strategy's fair price than a set multiple of the maker fee;
  - *re-quote band:* a resting quote is left alone, keeping its queue place, while it is within N ticks of the wanted price and still outside the fee floor;
  - *near expiry:* inside a set time before expiry only the side that reduces the position is quoted.

**Running several strategies.** Each selected strategy runs in its own paper account with its own hedger, on the same market data. Accounts don't compete for fills, which is a stated limit. One engine run can hold many accounts, e.g. all 4 strategies × 4 hedgers = 16 accounts in one pass.

**Hedgers** (one per account; all trade BTC-PERPETUAL as taker against the recorded perp book and pay the taker fee):
- *No hedge:* carry the delta, and report how much of the PnL it explains.
- *Fixed band:* hedge back to zero when delta leaves ±X BTC.
- *Time-based:* hedge to zero every N minutes.
- *Whalley–Wilmott:* the band width comes from gamma, the fee rate and a risk-aversion setting: half-width in BTC of delta = `(1.5 · fee rate · S · gamma² / risk aversion)^(1/3)`, with a small floor. Outside the band it trades back to the band's edge, not to zero. It is wider when trading costs more, narrower when you care more about risk.

A hedger sees the same delayed view as the strategy: delta from the fills reported so far and the view's tickers (options: Black-76 delta minus the premium in BTC, which is the delta of BTC-settled PnL; perpetual: USD position / price). It checks once a second, sends one immediate-or-cancel order at a time in whole 10 USD lots, and lets it fill up to 0.1% past the visible best price.

**Trading window.** A run can be limited to a window: nothing is quoted or hedged before its start (the books warm up first), quotes are cancelled at its end, and the run stops 70 s later so the last fills get their markouts. Positions still open are marked to mid there, not closed.

**Our orders.** Quotes are post-only: one that would cross the recorded book when it arrives is rejected, never filled as a taker. A replace is a cancel plus a new order; it keeps its queue place only if the price is unchanged and the size no larger. Hedge orders are immediate-or-cancel takers that walk the recorded book. An order can only be filled by a trade stamped inside its own lifetime (from its arrival to its cancel), whatever order the messages arrive in.

**Fill models:**
- *Optimistic:* we get filled whenever a recorded trade prints at or through our price, up to the trade's size.
- *Queue-aware:* our order joins the back of the queue at its price. Trades at that price use up the queue ahead of us first. Cancels at our level shrink the queue ahead of us in proportion to their size. A trade through our price means nothing was left ahead of us.
- *Trade-through setting* (`trade_through`, both models): how much a trade through our price fills. `trade_size` (default): at most the trade's size, because the aggressor only traded that much. `full`: the whole order. With either value the optimistic model never fills less than the queue-aware one.
- Maker fills are rounded down to whole lots (0.1 contract).

**Latency.** This is the network leg only; the feed's own delay comes from the market-data timing above. The default is the lowest measured one-way latency from the recorder's probe (best case). The sweep also covers 10 / 25 / 50 / 75 / 100 / 250 ms and the measured median, so results show how much they depend on speed.

**Accounting.** Each account tracks cash in BTC and positions marked to mid (the ticker's mark price when the book is one-sided or invalid), applying fees on every fill. At expiry, options settle at the Deribit delivery price (the `delivery_price` on the ticker sent after expiry; if it never arrives, the last `estimated_delivery_price`) minus the delivery fee. PnL is split into parts that sum to the total:
- *spread:* fill price vs mid at the fill (the mid at the trade's own timestamp, also when the trade print arrives late);
- *fees:* trading and delivery fees;
- *adverse selection:* the mid's move in the first 60 s after each fill;
- *option MTM:* the mid's move after those 60 s, including settlement;
- *hedge:* everything earned or lost on the perpetual, before fees.

Markouts (fill price vs mid 1s / 10s / 60s later) are reported per fill; the 60 s markout of a fill equals its spread plus its adverse-selection part.

**Determinism.** Same input plus same config gives byte-identical output files.

## 7. Acceptance criteria
Each milestone is checked against these.

1. **Recorder:** runs 7 days with reconnects logged. A check script rebuilds books from the raw files and matches a fresh REST `public/get_order_book` snapshot for at least 99% of sampled checks. Every gap appears in the log. The latency probe logs a value at least every 10 s while connected.
2. **Book rebuild in C++:** for one recorded hour, the C++ top-of-book matches the Python check script at every event, for every instrument.
3. **Pricing:** Black-76 prices and Greeks match reference values to 1e-8. The IV solver round-trips price → IV → price to 1e-10 over a grid of strikes, expiries and vols. Deep in/out-of-money cases return a clear "no solution" instead of NaN. The C++ Hagan formula matches `sabr.model.hagan_lognormal_vol` to 1e-12 on a grid, including near the money.
4. **SABR:**
   - The sabr repo's existing tests still pass after the changes.
   - On recorded data, the fit error against Deribit mark IV (median and worst strike, in vol points) is reported for β = 0.5 and β = 1.0.
   - No fit is used before `fit_time + compute time`.
5. **Fill models:** scenario tests pass (queue ahead, partial fills, cancels ahead, trade-through, latency delaying a fill or cancel). The optimistic model never fills less than the queue-aware one on the same input.
6. **Strategies and hedgers:** each passes scenario tests for its core rule. Examples: A-S shifts its quotes the right way with inventory; join-the-best stays out when the edge is too small; each hedger fires when, and only when, its rule says so.
7. **No look-ahead:** an injected future event is never visible to a strategy before `time + latency`.
8. **Accounting:** for every account at every step, cash + marked positions = starting equity + total PnL, and the PnL parts sum to the total (within 1e-9 BTC).
9. **Determinism:** two runs with the same input and config produce identical files.
10. **Speed:** one recorded day replays with all 16 accounts in under 5 minutes on the M2 Air. The target is checked in M2 and revised with the measured number if needed.
11. **Results:** on the 5-day report window of the 7-day dataset (parameters tuned on the first 2 days and fixed before the report window is run):
    - a table of 4 strategies × 2 fill models × the latency sweep (Whalley–Wilmott hedger);
    - a hedger table of 4 strategies × 4 hedgers (queue-aware, default latency).
    - Every row carries its conditions.
12. **Dashboard:**
    - The Strategies tab starts a session with any mix of the 4 strategies and 4 hedgers.
    - Live mode runs 1 hour without crashing, with flat memory.
    - Replay plays a recorded day at 1x–100x.
    - A demo video can be recorded from it.
13. **Honesty:** the README shows results after fees and adverse selection, whatever the sign, next to the known limits (no market impact, accounts don't compete, public-feed latency, fee date).
