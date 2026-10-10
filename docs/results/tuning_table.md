## Tuning table (tuning window, IN-SAMPLE: not a result)

Every parameter set tried. Runs a00.. are stage A (parameter sets); b00 is stage B (Whalley-Wilmott risk aversion on the picked sets).

Conditions shared by all 156 rows:

- window: tuning
- trade_from_utc: 2026-09-28T21:42:41Z
- trade_until_utc: 2026-09-30T21:42:41Z
- tuning_pick: grid
- sabr_beta: 0.5
- quote_size: 0.1
- max_position: 1
- max_vega_usd_per_vp: 100
- abs_delta_range: 0.1-0.9
- no_quote_before_expiry_s: 3600
- hedger: whalley_wilmott
- fill_model: queue_aware
- trade_through: trade_size
- md_timing: received
- md_latency_ms: 37.4635
- order_latency_ms: 37.4635
- recorded_latency_ms: 37.4635
- fee_schedule_date: 2026-09-27
- fee_tier: Standard (no VIP discount)
- starting_equity_btc: 1

| instruments_quoted | strategy | strategy_params | fair_value_source | fee_edge_mult | requote_band_ticks | reduce_only_before_expiry_s | hedger_params | pnl_btc | fees_btc | max_drawdown_btc | maker_fills | hedge_orders | account |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003797 | -0.002348 | 0.005180 | 59 | 1096 | fixed_spread-00 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.004088 | -0.002337 | 0.005472 | 58 | 1132 | fixed_spread-01 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.008206 | -0.006362 | 0.010720 | 162 | 1593 | fixed_spread-02 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.008426 | -0.006226 | 0.010751 | 158 | 1509 | fixed_spread-03 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003601 | -0.002326 | 0.005023 | 57 | 1181 | fixed_spread-04 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003881 | -0.002315 | 0.005303 | 56 | 1257 | fixed_spread-05 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.004320 | -0.003206 | 0.005589 | 84 | 1000 | fixed_spread-06 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.004331 | -0.003182 | 0.005600 | 82 | 1147 | fixed_spread-07 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001831 | -0.000652 | 0.002278 | 14 | 496 | fixed_spread-08 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001831 | -0.000652 | 0.002278 | 14 | 496 | fixed_spread-09 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001715 | -0.000725 | 0.002497 | 15 | 514 | fixed_spread-10 |
| 130 | fixed_spread | {"half_spread_vp": 2.0} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001715 | -0.000725 | 0.002497 | 15 | 514 | fixed_spread-11 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001855 | -0.000355 | 0.002141 | 6 | 492 | fixed_spread-12 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001855 | -0.000355 | 0.002141 | 6 | 492 | fixed_spread-13 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002922 | -0.001178 | 0.003711 | 24 | 1035 | fixed_spread-14 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002922 | -0.001178 | 0.003711 | 24 | 1035 | fixed_spread-15 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001855 | -0.000355 | 0.002141 | 6 | 492 | fixed_spread-16 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001855 | -0.000355 | 0.002141 | 6 | 492 | fixed_spread-17 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003248 | -0.001092 | 0.003820 | 21 | 1118 | fixed_spread-18 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003248 | -0.001092 | 0.003820 | 21 | 1118 | fixed_spread-19 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001534 | -0.000257 | 0.001806 | 5 | 408 | fixed_spread-20 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001534 | -0.000257 | 0.001806 | 5 | 408 | fixed_spread-21 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001507 | -0.000531 | 0.002000 | 11 | 480 | fixed_spread-22 |
| 130 | fixed_spread | {"half_spread_vp": 4.0} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001507 | -0.000531 | 0.002000 | 11 | 480 | fixed_spread-23 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-24 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-25 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000585 | -0.000321 | 0.001065 | 5 | 353 | fixed_spread-26 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000585 | -0.000321 | 0.001065 | 5 | 353 | fixed_spread-27 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-28 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-29 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000585 | -0.000321 | 0.001065 | 5 | 353 | fixed_spread-30 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000585 | -0.000321 | 0.001065 | 5 | 353 | fixed_spread-31 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-32 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-33 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000572 | -0.000189 | 0.000786 | 3 | 244 | fixed_spread-34 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000572 | -0.000189 | 0.000786 | 3 | 244 | fixed_spread-35 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | -0.000664 | -0.000041 | 0.000894 | 1 | 12 | fixed_spread-ra0 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000179 | -0.000050 | 0.000359 | 1 | 14 | fixed_spread-ra1 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.01} | +0.000014 | -0.000061 | 0.000209 | 1 | 60 | fixed_spread-ra2 |
| 130 | fixed_spread | {"half_spread_vp": 6.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.1} | +0.000068 | -0.000077 | 0.000157 | 1 | 238 | fixed_spread-ra3 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000851 | -0.001067 | 0.002021 | 29 | 747 | inventory_skew-00 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001051 | -0.001056 | 0.002221 | 28 | 863 | inventory_skew-01 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001179 | -0.001547 | 0.002492 | 43 | 917 | inventory_skew-02 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001191 | -0.001523 | 0.002492 | 41 | 1064 | inventory_skew-03 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001368 | -0.000396 | 0.002035 | 8 | 400 | inventory_skew-04 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001368 | -0.000396 | 0.002035 | 8 | 400 | inventory_skew-05 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001304 | -0.000435 | 0.002150 | 9 | 397 | inventory_skew-06 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001304 | -0.000435 | 0.002150 | 9 | 397 | inventory_skew-07 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000902 | -0.001117 | 0.002131 | 30 | 772 | inventory_skew-08 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001102 | -0.001106 | 0.002331 | 29 | 888 | inventory_skew-09 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000751 | -0.001486 | 0.002371 | 41 | 916 | inventory_skew-10 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000762 | -0.001462 | 0.002371 | 39 | 1063 | inventory_skew-11 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001646 | -0.000355 | 0.002003 | 7 | 412 | inventory_skew-12 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001646 | -0.000355 | 0.002003 | 7 | 412 | inventory_skew-13 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001304 | -0.000435 | 0.002150 | 9 | 397 | inventory_skew-14 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001304 | -0.000435 | 0.002150 | 9 | 397 | inventory_skew-15 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001137 | -0.000231 | 0.001488 | 3 | 379 | inventory_skew-16 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001137 | -0.000231 | 0.001488 | 3 | 379 | inventory_skew-17 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001930 | -0.000432 | 0.002163 | 6 | 1025 | inventory_skew-18 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001930 | -0.000432 | 0.002163 | 6 | 1025 | inventory_skew-19 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000817 | -0.000133 | 0.001153 | 2 | 295 | inventory_skew-20 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000817 | -0.000133 | 0.001153 | 2 | 295 | inventory_skew-21 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001383 | -0.000187 | 0.001602 | 3 | 461 | inventory_skew-22 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 0.5, "vol_widen": 0.25} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001383 | -0.000187 | 0.001602 | 3 | 461 | inventory_skew-23 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001137 | -0.000231 | 0.001488 | 3 | 379 | inventory_skew-24 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001137 | -0.000231 | 0.001488 | 3 | 379 | inventory_skew-25 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001646 | -0.000395 | 0.001980 | 5 | 973 | inventory_skew-26 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001646 | -0.000395 | 0.001980 | 5 | 973 | inventory_skew-27 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000817 | -0.000133 | 0.001153 | 2 | 295 | inventory_skew-28 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000817 | -0.000133 | 0.001153 | 2 | 295 | inventory_skew-29 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001099 | -0.000149 | 0.001420 | 2 | 409 | inventory_skew-30 |
| 130 | inventory_skew | {"half_spread_vp": 4.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001099 | -0.000149 | 0.001420 | 2 | 409 | inventory_skew-31 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | +0.000562 | -0.001271 | 0.002533 | 41 | 351 | inventory_skew-ra0 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000751 | -0.001486 | 0.002371 | 41 | 916 | inventory_skew-ra1 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.01} | -0.001146 | -0.001825 | 0.001880 | 41 | 1801 | inventory_skew-ra2 |
| 130 | inventory_skew | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.1} | -0.001330 | -0.002254 | 0.002020 | 41 | 3119 | inventory_skew-ra3 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.010149 | -0.005973 | 0.012297 | 141 | 1628 | join_best-00 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.010125 | -0.005966 | 0.012272 | 140 | 1653 | join_best-01 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.010996 | -0.008296 | 0.013110 | 221 | 1684 | join_best-02 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.010755 | -0.008220 | 0.012870 | 216 | 1688 | join_best-03 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.008259 | -0.003537 | 0.010047 | 79 | 1561 | join_best-04 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.008303 | -0.003534 | 0.010091 | 78 | 1588 | join_best-05 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.009924 | -0.004277 | 0.011981 | 89 | 1475 | join_best-06 |
| 130 | join_best | {"min_edge_vp": 1.0} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.009721 | -0.004265 | 0.011778 | 88 | 1518 | join_best-07 |
| 120 | join_best | {"min_edge_vp": 1.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-08 |
| 120 | join_best | {"min_edge_vp": 1.0} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-09 |
| 120 | join_best | {"min_edge_vp": 1.0} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-10 |
| 120 | join_best | {"min_edge_vp": 1.0} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-11 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002663 | -0.001348 | 0.004705 | 30 | 1019 | join_best-12 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002663 | -0.001348 | 0.004705 | 30 | 1019 | join_best-13 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.004222 | -0.001900 | 0.006198 | 41 | 1058 | join_best-14 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.004222 | -0.001900 | 0.006198 | 41 | 1058 | join_best-15 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002315 | -0.001295 | 0.004386 | 27 | 1137 | join_best-16 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002315 | -0.001295 | 0.004386 | 27 | 1137 | join_best-17 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002531 | -0.001420 | 0.004518 | 31 | 961 | join_best-18 |
| 130 | join_best | {"min_edge_vp": 2.0} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.002531 | -0.001420 | 0.004518 | 31 | 961 | join_best-19 |
| 120 | join_best | {"min_edge_vp": 2.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-20 |
| 120 | join_best | {"min_edge_vp": 2.0} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-21 |
| 120 | join_best | {"min_edge_vp": 2.0} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-22 |
| 120 | join_best | {"min_edge_vp": 2.0} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000330 | -0.000267 | 0.001587 | 6 | 255 | join_best-23 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000464 | -0.000094 | 0.000607 | 1 | 195 | join_best-24 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000464 | -0.000094 | 0.000607 | 1 | 195 | join_best-25 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000454 | -0.000094 | 0.000607 | 1 | 195 | join_best-26 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000454 | -0.000094 | 0.000607 | 1 | 195 | join_best-27 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000464 | -0.000094 | 0.000607 | 1 | 195 | join_best-28 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000464 | -0.000094 | 0.000607 | 1 | 195 | join_best-29 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000454 | -0.000094 | 0.000607 | 1 | 195 | join_best-30 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000454 | -0.000094 | 0.000607 | 1 | 195 | join_best-31 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-32 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-33 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-34 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-35 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-ra0 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-ra1 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.01} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-ra2 |
| 101 | join_best | {"min_edge_vp": 4.0} | sabr | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.1} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | join_best-ra3 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001197 | -0.002098 | 0.003742 | 67 | 825 | mark_iv_follower-00 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001159 | -0.001896 | 0.003719 | 50 | 789 | mark_iv_follower-01 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003175 | -0.005520 | 0.004602 | 162 | 1152 | mark_iv_follower-02 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.003991 | -0.005117 | 0.004955 | 141 | 1102 | mark_iv_follower-03 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001402 | -0.002059 | 0.002540 | 63 | 959 | mark_iv_follower-04 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001762 | -0.001900 | 0.002900 | 48 | 1143 | mark_iv_follower-05 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000425 | -0.003044 | 0.001789 | 95 | 976 | mark_iv_follower-06 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000266 | -0.002726 | 0.001789 | 78 | 1002 | mark_iv_follower-07 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000931 | -0.000692 | 0.000666 | 21 | 304 | mark_iv_follower-08 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000322 | -0.000461 | 0.000666 | 11 | 249 | mark_iv_follower-09 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000564 | -0.000828 | 0.000740 | 21 | 98 | mark_iv_follower-10 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000057 | -0.000608 | 0.000740 | 14 | 75 | mark_iv_follower-11 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000898 | -0.000411 | 0.001289 | 10 | 285 | mark_iv_follower-12 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000964 | -0.000319 | 0.001355 | 5 | 316 | mark_iv_follower-13 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001051 | -0.001271 | 0.003708 | 31 | 722 | mark_iv_follower-14 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001428 | -0.001011 | 0.003708 | 21 | 941 | mark_iv_follower-15 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000898 | -0.000411 | 0.001289 | 10 | 285 | mark_iv_follower-16 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000964 | -0.000319 | 0.001355 | 5 | 316 | mark_iv_follower-17 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001239 | -0.001150 | 0.001837 | 27 | 749 | mark_iv_follower-18 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.001942 | -0.000941 | 0.002540 | 19 | 878 | mark_iv_follower-19 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000004 | -0.000284 | 0.000605 | 8 | 48 | mark_iv_follower-20 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000155 | -0.000126 | 0.000557 | 3 | 19 | mark_iv_follower-21 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000349 | -0.000555 | 0.000978 | 13 | 67 | mark_iv_follower-22 |
| 130 | mark_iv_follower | {"half_spread_vp": 4.0} | deribit_mark_iv | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000507 | -0.000469 | 0.000978 | 10 | 26 | mark_iv_follower-23 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 0 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000090 | -0.000010 | 0.000145 | 1 | 0 | mark_iv_follower-24 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 0 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | mark_iv_follower-25 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 0 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000125 | -0.000362 | 0.001139 | 6 | 399 | mark_iv_follower-26 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 0 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000626 | -0.000200 | 0.001504 | 3 | 214 | mark_iv_follower-27 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 1 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000090 | -0.000010 | 0.000145 | 1 | 0 | mark_iv_follower-28 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 1 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | mark_iv_follower-29 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 1 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000021 | -0.000222 | 0.000608 | 4 | 358 | mark_iv_follower-30 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 1 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000309 | -0.000139 | 0.000680 | 2 | 204 | mark_iv_follower-31 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | -0.000090 | -0.000010 | 0.000145 | 1 | 0 | mark_iv_follower-32 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 2 | 0 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000000 | +0.000000 | 0.000000 | 0 | 0 | mark_iv_follower-33 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 2 | 1 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000462 | -0.000125 | 0.000192 | 3 | 159 | mark_iv_follower-34 |
| 130 | mark_iv_follower | {"half_spread_vp": 6.0} | deribit_mark_iv | 2 | 1 | 14400 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000149 | -0.000043 | 0.000192 | 1 | 9 | mark_iv_follower-35 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001} | +0.001617 | -0.000622 | 0.000594 | 21 | 249 | mark_iv_follower-ra0 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.001} | +0.000931 | -0.000692 | 0.000666 | 21 | 304 | mark_iv_follower-ra1 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.01} | +0.000635 | -0.000740 | 0.000665 | 21 | 389 | mark_iv_follower-ra2 |
| 130 | mark_iv_follower | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | 3600 | {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.1} | +0.000344 | -0.000792 | 0.000602 | 21 | 538 | mark_iv_follower-ra3 |
