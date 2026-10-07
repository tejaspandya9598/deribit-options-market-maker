## Strategy table (report window, out of sample)

4 strategies x 2 fill models x the latency sweep, Whalley-Wilmott hedger. Parameters were fixed on the tuning window before these runs.

Conditions shared by all 96 rows:

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
- hedger: whalley_wilmott
- hedger_params: {"min_band_btc": 0.005, "risk_aversion_per_usd": 0.0001}
- trade_through: trade_size
- md_timing: received
- recorded_latency_ms: 37.4635
- fee_schedule_date: 2026-09-27
- fee_tier: Standard (no VIP discount)
- starting_equity_btc: 1

| instruments_quoted | strategy | tuning_pick | strategy_params | fair_value_source | fee_edge_mult | requote_band_ticks | fill_model | md_latency_ms | order_latency_ms | pnl_btc | pnl_usd | spread_btc | fees_btc | adverse_60s_btc | option_mtm_btc | hedge_btc | max_drawdown_btc | maker_fills | hedge_orders |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 10 | 10 | -0.001344 | -115.42 | +0.000800 | -0.001085 | -0.000290 | +0.001757 | -0.002525 | 0.004041 | 18 | 326 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 25 | 25 | -0.001343 | -115.42 | +0.000800 | -0.001085 | -0.000290 | +0.001757 | -0.002525 | 0.004041 | 18 | 326 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 37.4635 | 37.4635 | -0.001343 | -115.41 | +0.000800 | -0.001085 | -0.000290 | +0.001757 | -0.002524 | 0.004041 | 18 | 326 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 44.423 | 44.423 | -0.001343 | -115.41 | +0.000800 | -0.001085 | -0.000290 | +0.001757 | -0.002524 | 0.004041 | 18 | 326 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 50 | 50 | -0.001343 | -115.41 | +0.000800 | -0.001085 | -0.000290 | +0.001757 | -0.002524 | 0.004041 | 18 | 326 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 75 | 75 | -0.001943 | -166.89 | +0.000900 | -0.001186 | -0.000290 | +0.000533 | -0.001900 | 0.004544 | 19 | 339 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 100 | 100 | -0.001619 | -139.11 | +0.000750 | -0.001085 | -0.000340 | +0.001407 | -0.002350 | 0.004171 | 18 | 337 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | optimistic | 250 | 250 | +0.000268 | +22.98 | +0.000460 | -0.000816 | -0.000375 | -0.000143 | +0.001142 | 0.003561 | 15 | 123 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 10 | 10 | +0.001514 | +130.07 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 25 | 25 | +0.001514 | +130.07 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 37.4635 | 37.4635 | +0.001514 | +130.07 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 44.423 | 44.423 | +0.001514 | +130.07 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 50 | 50 | +0.001514 | +130.08 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 75 | 75 | +0.001514 | +130.06 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 100 | 100 | +0.001514 | +130.06 | -0.000340 | -0.000301 | -0.000580 | +0.003451 | -0.000716 | 0.001065 | 7 | 38 |
| 240 | fixed_spread | best_pnl | {"half_spread_vp": 6.0} | sabr | 0 | 0 | queue_aware | 250 | 250 | +0.001420 | +121.98 | -0.000405 | -0.000261 | -0.000465 | +0.002751 | -0.000200 | 0.001065 | 6 | 20 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 10 | 10 | -0.007869 | -676.05 | +0.002710 | -0.004039 | -0.003045 | +0.003127 | -0.006622 | 0.015969 | 83 | 981 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 25 | 25 | -0.007848 | -674.25 | +0.002710 | -0.004039 | -0.003045 | +0.003127 | -0.006601 | 0.015969 | 83 | 981 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 37.4635 | 37.4635 | -0.009120 | -783.50 | +0.002735 | -0.004135 | -0.003220 | +0.002327 | -0.006826 | 0.015969 | 84 | 992 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 44.423 | 44.423 | -0.009120 | -783.50 | +0.002735 | -0.004135 | -0.003220 | +0.002327 | -0.006826 | 0.015968 | 84 | 992 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 50 | 50 | -0.009120 | -783.50 | +0.002735 | -0.004135 | -0.003220 | +0.002327 | -0.006826 | 0.015968 | 84 | 992 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 75 | 75 | -0.009105 | -782.27 | +0.002735 | -0.004136 | -0.003220 | +0.002327 | -0.006812 | 0.015968 | 84 | 993 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 100 | 100 | -0.008723 | -749.38 | +0.002725 | -0.004080 | -0.002865 | +0.001832 | -0.006334 | 0.015968 | 83 | 977 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | optimistic | 250 | 250 | -0.009421 | -809.38 | +0.002765 | -0.004184 | -0.003920 | +0.004985 | -0.009066 | 0.016179 | 86 | 1214 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 10 | 10 | -0.007290 | -626.27 | +0.001915 | -0.003108 | -0.002310 | +0.003690 | -0.007476 | 0.011346 | 60 | 1007 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 25 | 25 | -0.007268 | -624.39 | +0.001915 | -0.003108 | -0.002310 | +0.003690 | -0.007454 | 0.011346 | 60 | 1007 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 37.4635 | 37.4635 | -0.008861 | -761.23 | +0.001940 | -0.003202 | -0.002485 | +0.002890 | -0.008004 | 0.012018 | 61 | 1230 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 44.423 | 44.423 | -0.008861 | -761.22 | +0.001940 | -0.003202 | -0.002485 | +0.002890 | -0.008003 | 0.012018 | 61 | 1231 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 50 | 50 | -0.008861 | -761.23 | +0.001940 | -0.003202 | -0.002485 | +0.002890 | -0.008003 | 0.012018 | 61 | 1232 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 75 | 75 | -0.008839 | -759.41 | +0.001940 | -0.003202 | -0.002485 | +0.002890 | -0.007982 | 0.011996 | 61 | 1234 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 100 | 100 | -0.008498 | -730.09 | +0.001840 | -0.003101 | -0.002535 | +0.003763 | -0.008465 | 0.011655 | 60 | 1234 |
| 240 | fixed_spread | best_pnl_min_fills | {"half_spread_vp": 4.0} | sabr | 0 | 1 | queue_aware | 250 | 250 | -0.007883 | -677.28 | +0.001815 | -0.003098 | -0.003105 | +0.006619 | -0.010115 | 0.011039 | 62 | 1286 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 10 | 10 | -0.017248 | -1481.82 | +0.005680 | -0.007903 | -0.004085 | +0.027408 | -0.038348 | 0.027256 | 208 | 1734 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 25 | 25 | -0.017726 | -1522.84 | +0.005740 | -0.007954 | -0.003975 | +0.026688 | -0.038224 | 0.027734 | 210 | 1736 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 37.4635 | 37.4635 | -0.017725 | -1522.80 | +0.005740 | -0.007954 | -0.003975 | +0.026688 | -0.038224 | 0.027733 | 210 | 1737 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 44.423 | 44.423 | -0.017684 | -1519.29 | +0.005710 | -0.007910 | -0.003860 | +0.026243 | -0.037867 | 0.027692 | 209 | 1742 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 50 | 50 | -0.017682 | -1519.08 | +0.005710 | -0.007910 | -0.003860 | +0.026243 | -0.037865 | 0.027690 | 209 | 1743 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 75 | 75 | -0.017964 | -1543.35 | +0.005735 | -0.007929 | -0.003855 | +0.026003 | -0.037918 | 0.027972 | 210 | 1745 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 100 | 100 | -0.018000 | -1546.39 | +0.005735 | -0.007912 | -0.003550 | +0.025558 | -0.037831 | 0.028008 | 209 | 1785 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | optimistic | 250 | 250 | -0.017731 | -1523.31 | +0.005490 | -0.007764 | -0.004000 | +0.027841 | -0.039297 | 0.025366 | 205 | 1744 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 10 | 10 | -0.009944 | -854.34 | +0.002995 | -0.004737 | -0.003685 | +0.018314 | -0.022831 | 0.014022 | 130 | 1209 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 25 | 25 | -0.009941 | -854.09 | +0.002995 | -0.004737 | -0.003685 | +0.018314 | -0.022828 | 0.014019 | 130 | 1210 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 37.4635 | 37.4635 | -0.009940 | -854.00 | +0.002995 | -0.004737 | -0.003685 | +0.018314 | -0.022827 | 0.014019 | 130 | 1210 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 44.423 | 44.423 | -0.009812 | -842.94 | +0.002965 | -0.004689 | -0.003570 | +0.017869 | -0.022387 | 0.013891 | 129 | 1220 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 50 | 50 | -0.009809 | -842.72 | +0.002965 | -0.004689 | -0.003570 | +0.017869 | -0.022384 | 0.013889 | 129 | 1221 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 75 | 75 | -0.008486 | -729.08 | +0.002935 | -0.004640 | -0.003535 | +0.017983 | -0.021229 | 0.013927 | 128 | 1161 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 100 | 100 | -0.008620 | -740.59 | +0.002710 | -0.004482 | -0.003255 | +0.019235 | -0.022828 | 0.013819 | 125 | 1151 |
| 240 | inventory_skew | best_pnl | {"half_spread_vp": 2.0, "inventory_widen_vp": 1.0, "max_vol_widen_vp": 5.0, "skew_vp_per_100_vega": 2.0, "vol_widen": 0.25} | sabr | 1 | 1 | queue_aware | 250 | 250 | -0.009301 | -799.07 | +0.002735 | -0.004578 | -0.003510 | +0.018716 | -0.022664 | 0.013786 | 127 | 1358 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 10 | 10 | -0.002578 | -221.50 | +0.003455 | -0.001656 | -0.000255 | -0.002206 | -0.001917 | 0.008860 | 25 | 561 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 25 | 25 | -0.002743 | -235.69 | +0.003505 | -0.001686 | -0.000180 | -0.002831 | -0.001552 | 0.009026 | 26 | 566 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 37.4635 | 37.4635 | -0.002743 | -235.68 | +0.003505 | -0.001686 | -0.000180 | -0.002831 | -0.001552 | 0.009025 | 26 | 566 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 44.423 | 44.423 | -0.002793 | -239.97 | +0.003455 | -0.001686 | -0.000180 | -0.002831 | -0.001551 | 0.009025 | 26 | 566 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 50 | 50 | -0.003138 | -269.55 | +0.003480 | -0.001739 | -0.000080 | -0.002106 | -0.002692 | 0.009143 | 27 | 562 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 75 | 75 | -0.003149 | -270.51 | +0.003480 | -0.001739 | -0.000080 | -0.002106 | -0.002703 | 0.009154 | 27 | 562 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 100 | 100 | -0.003146 | -270.28 | +0.003480 | -0.001739 | -0.000080 | -0.002106 | -0.002701 | 0.009151 | 27 | 562 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | optimistic | 250 | 250 | -0.004170 | -358.23 | +0.003285 | -0.001690 | -0.000230 | -0.002393 | -0.003142 | 0.010175 | 25 | 618 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 10 | 10 | -0.001005 | -86.30 | +0.000350 | -0.000225 | -0.000150 | -0.000897 | -0.000083 | 0.001496 | 3 | 125 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 25 | 25 | -0.001004 | -86.30 | +0.000350 | -0.000225 | -0.000150 | -0.000897 | -0.000083 | 0.001496 | 3 | 125 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 37.4635 | 37.4635 | -0.001004 | -86.29 | +0.000350 | -0.000225 | -0.000150 | -0.000897 | -0.000083 | 0.001496 | 3 | 125 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 44.423 | 44.423 | -0.000665 | -57.17 | +0.000400 | -0.000264 | -0.000275 | -0.000207 | -0.000319 | 0.001759 | 4 | 176 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 50 | 50 | -0.000665 | -57.17 | +0.000400 | -0.000264 | -0.000275 | -0.000207 | -0.000319 | 0.001759 | 4 | 176 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 75 | 75 | -0.000666 | -57.18 | +0.000400 | -0.000264 | -0.000275 | -0.000207 | -0.000319 | 0.001759 | 4 | 176 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 100 | 100 | -0.000665 | -57.17 | +0.000400 | -0.000264 | -0.000275 | -0.000207 | -0.000319 | 0.001759 | 4 | 176 |
| 206 | join_best | best_pnl | {"min_edge_vp": 4.0} | sabr | 2 | 0 | queue_aware | 250 | 250 | -0.000669 | -57.49 | +0.000400 | -0.000264 | -0.000275 | -0.000207 | -0.000323 | 0.001762 | 4 | 177 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 10 | 10 | -0.009233 | -793.25 | +0.010915 | -0.011404 | -0.003200 | +0.021576 | -0.027120 | 0.025514 | 294 | 1528 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 25 | 25 | -0.009763 | -838.77 | +0.010965 | -0.011431 | -0.003125 | +0.020951 | -0.027123 | 0.026044 | 295 | 1528 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 37.4635 | 37.4635 | -0.009900 | -850.55 | +0.010925 | -0.011363 | -0.003130 | +0.020881 | -0.027213 | 0.026163 | 293 | 1523 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 44.423 | 44.423 | -0.009950 | -854.82 | +0.010875 | -0.011363 | -0.003130 | +0.020881 | -0.027213 | 0.026163 | 293 | 1523 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 50 | 50 | -0.010244 | -880.11 | +0.010865 | -0.011370 | -0.003225 | +0.020686 | -0.027200 | 0.026457 | 293 | 1546 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 75 | 75 | -0.011101 | -953.69 | +0.010770 | -0.011317 | -0.003100 | +0.019766 | -0.027219 | 0.027370 | 290 | 1617 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 100 | 100 | -0.011150 | -957.95 | +0.010845 | -0.011492 | -0.003200 | +0.018614 | -0.025918 | 0.027447 | 293 | 1640 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | optimistic | 250 | 250 | -0.010813 | -928.97 | +0.010535 | -0.011254 | -0.003245 | +0.020138 | -0.026988 | 0.027117 | 284 | 1663 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 10 | 10 | -0.003573 | -307.00 | +0.002050 | -0.002713 | -0.002180 | +0.010289 | -0.011019 | 0.006456 | 69 | 1321 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 25 | 25 | -0.003271 | -281.05 | +0.001975 | -0.002654 | -0.001930 | +0.009314 | -0.009976 | 0.006453 | 68 | 1280 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 37.4635 | 37.4635 | -0.003271 | -280.98 | +0.001975 | -0.002653 | -0.001930 | +0.009314 | -0.009976 | 0.006453 | 68 | 1279 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 44.423 | 44.423 | -0.002897 | -248.91 | +0.002025 | -0.002675 | -0.002055 | +0.010004 | -0.010196 | 0.006453 | 69 | 1264 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 50 | 50 | -0.002897 | -248.88 | +0.002025 | -0.002674 | -0.002055 | +0.010004 | -0.010196 | 0.006453 | 69 | 1264 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 75 | 75 | -0.002709 | -232.74 | +0.002025 | -0.002666 | -0.001780 | +0.009729 | -0.010017 | 0.006454 | 69 | 1231 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 100 | 100 | -0.002568 | -220.65 | +0.002040 | -0.002684 | -0.001755 | +0.009979 | -0.010148 | 0.006451 | 70 | 1233 |
| 235 | join_best | best_pnl_min_fills | {"min_edge_vp": 2.0} | sabr | 1 | 0 | queue_aware | 250 | 250 | -0.001539 | -132.23 | +0.001970 | -0.002709 | -0.002180 | +0.011491 | -0.010111 | 0.007324 | 67 | 1110 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 10 | 10 | -0.002858 | -245.58 | +0.003485 | -0.004371 | -0.001595 | +0.001782 | -0.002160 | 0.008594 | 101 | 922 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 25 | 25 | -0.003092 | -265.66 | +0.003535 | -0.004425 | -0.001520 | +0.001157 | -0.001840 | 0.008954 | 102 | 941 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 37.4635 | 37.4635 | -0.003390 | -291.25 | +0.003555 | -0.004538 | -0.001585 | +0.001348 | -0.002171 | 0.009211 | 103 | 1040 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 44.423 | 44.423 | -0.001188 | -102.02 | +0.003605 | -0.004637 | -0.001635 | +0.003388 | -0.001909 | 0.009233 | 104 | 1114 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 50 | 50 | -0.001188 | -102.02 | +0.003605 | -0.004637 | -0.001635 | +0.003388 | -0.001909 | 0.009233 | 104 | 1114 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 75 | 75 | -0.001190 | -102.22 | +0.003605 | -0.004637 | -0.001635 | +0.003388 | -0.001911 | 0.009233 | 104 | 1114 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 100 | 100 | -0.001430 | -122.82 | +0.003630 | -0.004637 | -0.001585 | +0.002713 | -0.001551 | 0.009233 | 104 | 1117 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | optimistic | 250 | 250 | -0.000133 | -11.42 | +0.003525 | -0.004480 | -0.001715 | +0.003918 | -0.001381 | 0.007653 | 103 | 1017 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 10 | 10 | -0.001485 | -127.56 | +0.000795 | -0.002053 | -0.001470 | +0.006928 | -0.005685 | 0.003902 | 43 | 620 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 25 | 25 | -0.001485 | -127.57 | +0.000795 | -0.002053 | -0.001470 | +0.006928 | -0.005685 | 0.003902 | 43 | 620 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 37.4635 | 37.4635 | -0.001508 | -129.60 | +0.000815 | -0.002111 | -0.001535 | +0.007119 | -0.005797 | 0.003950 | 44 | 572 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 44.423 | 44.423 | -0.001836 | -157.76 | +0.000865 | -0.002207 | -0.001585 | +0.009159 | -0.008069 | 0.004662 | 45 | 694 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 50 | 50 | -0.001836 | -157.76 | +0.000865 | -0.002207 | -0.001585 | +0.009159 | -0.008069 | 0.004662 | 45 | 694 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 75 | 75 | -0.001837 | -157.84 | +0.000865 | -0.002207 | -0.001585 | +0.009159 | -0.008069 | 0.004663 | 45 | 694 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 100 | 100 | -0.001329 | -114.16 | +0.000690 | -0.002052 | -0.001485 | +0.008368 | -0.006849 | 0.004006 | 43 | 566 |
| 240 | mark_iv_follower | best_pnl | {"half_spread_vp": 2.0} | deribit_mark_iv | 2 | 0 | queue_aware | 250 | 250 | -0.001387 | -119.13 | +0.000575 | -0.002021 | -0.001370 | +0.009391 | -0.007962 | 0.004403 | 42 | 728 |
