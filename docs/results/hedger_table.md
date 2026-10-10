## Hedger table (report window, out of sample)

4 strategies x 4 hedgers, queue-aware fills, lowest measured latency. Parameters were fixed on the tuning window before this run.

Conditions shared by all 16 rows:

- window: report
- trade_from_utc: 2026-09-30T21:42:41Z
- trade_until_utc: 2026-10-05T21:42:41Z
- sabr_beta: 0.5
- quote_size: 0.1
- max_position: 1
- max_vega_usd_per_vp: 100
- abs_delta_range: 0.1-0.9
- reduce_only_before_expiry_s: 3600
- no_quote_before_expiry_s: 3600
- fill_model: queue_aware
- trade_through: trade_size
- md_timing: received
- md_latency_ms: 37.4635
- order_latency_ms: 37.4635
- recorded_latency_ms: 37.4635
- fee_schedule_date: 2026-09-27
- fee_tier: Standard (no VIP discount)
- starting_equity_btc: 1

| instruments_quoted | strategy | tuning_pick | strategy_params | fair_value_source | fee_edge_mult | requote_band_ticks | hedger | hedger_params | pnl_btc | pnl_usd | spread_btc | fees_btc | adverse_60s_btc | option_mtm_btc | hedge_btc | max_drawdown_btc | maker_fills | hedge_orders | delta_explained_btc | perp_volume_usd |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | fixed_band | {"band_btc": 0.05} | -0.009508 | -816.82 | +0.001940 | -0.008417 | -0.002485 | +0.002890 | -0.003435 | 0.012516 | 61 | 275 |  | 1.62825e+06 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | none | {} | +0.000600 | +51.51 | +0.001940 | -0.001745 | -0.002485 | +0.002890 | +0.000000 | 0.012043 | 61 | 0 | +0.001252 | 0 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | time_based | {"interval_min": 15.0} | -0.005932 | -509.67 | +0.001940 | -0.005525 | -0.002485 | +0.002890 | -0.002752 | 0.009001 | 61 | 389 |  | 921530 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | whalley_wilmott | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | -0.008861 | -761.23 | +0.001940 | -0.003202 | -0.002485 | +0.002890 | -0.008004 | 0.011988 | 61 | 1230 |  | 355140 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | fixed_band | {"band_btc": 0.05} | -0.015105 | -1297.66 | +0.002995 | -0.012281 | -0.003685 | +0.018314 | -0.020447 | 0.016272 | 130 | 417 |  | 2.21485e+06 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | none | {} | +0.014406 | +1237.65 | +0.002995 | -0.003218 | -0.003685 | +0.018314 | +0.000000 | 0.029064 | 130 | 0 | +0.016740 | 0 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | time_based | {"interval_min": 15.0} | -0.010774 | -925.61 | +0.002995 | -0.008247 | -0.003685 | +0.018314 | -0.020150 | 0.011646 | 130 | 446 |  | 1.22642e+06 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | whalley_wilmott | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | -0.009940 | -854.00 | +0.002995 | -0.004737 | -0.003685 | +0.018314 | -0.022827 | 0.014019 | 130 | 1210 |  | 371230 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | fixed_band | {"band_btc": 0.05} | -0.005639 | -484.48 | +0.001975 | -0.004509 | -0.001930 | +0.009314 | -0.010489 | 0.006967 | 68 | 128 |  | 664000 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | none | {} | +0.007556 | +649.16 | +0.001975 | -0.001803 | -0.001930 | +0.009314 | +0.000000 | 0.014412 | 68 | 0 | +0.007597 | 0 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | time_based | {"interval_min": 15.0} | -0.003852 | -330.89 | +0.001975 | -0.004290 | -0.001930 | +0.009314 | -0.008920 | 0.004975 | 68 | 386 |  | 609050 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | whalley_wilmott | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | -0.003271 | -280.98 | +0.001975 | -0.002653 | -0.001930 | +0.009314 | -0.009976 | 0.006463 | 68 | 1279 |  | 207420 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | fixed_band | {"band_btc": 0.05} | -0.006081 | -522.41 | +0.000815 | -0.004063 | -0.001535 | +0.007119 | -0.008417 | 0.007009 | 44 | 112 |  | 728320 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | none | {} | +0.005327 | +457.62 | +0.000815 | -0.001072 | -0.001535 | +0.007119 | +0.000000 | 0.009868 | 44 | 0 | +0.006214 | 0 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | time_based | {"interval_min": 15.0} | -0.003402 | -292.30 | +0.000815 | -0.003333 | -0.001535 | +0.007119 | -0.006468 | 0.004681 | 44 | 394 |  | 550300 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | whalley_wilmott | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | -0.001508 | -129.60 | +0.000815 | -0.002111 | -0.001535 | +0.007119 | -0.005797 | 0.003950 | 44 | 572 |  | 253300 |
