# Deribit fees (BTC options + BTC-PERPETUAL), checked 2026-09-27

Official source: Deribit Support "Fees" page
(https://support.deribit.com/hc/en-us/articles/25944746248989-Fees), saved as
`Fees – Deribit Support.pdf` in this folder on 2026-09-27 (page showed "Updated 2 days ago").
Cross-checked against the public API the same day.

## Trading fees, Standard tier (no VIP discount)
| Product | Maker | Taker | Cap |
|---|---|---|---|
| Options | 3 bps (0.0003) | 3 bps (0.0003) | 12.5% of the option premium |
| Perpetuals and futures | 1.5 bps (0.00015) | 3.5 bps (0.00035) | none |

The API agrees: `public/get_instruments` gives maker/taker_commission 0.0003 for BTC options, and
`public/get_instrument` gives 0.00015 / 0.00035 for BTC-PERPETUAL.

Option fee per contract (contract size 1 BTC): `min(0.0003, 0.125 × option price in BTC)` BTC.
From the page: "Fees on options trades or deliveries cannot exceed 12.5% of the option's value."

## Delivery fees (settlement at expiry)
| Product | Fee |
|---|---|
| BTC/ETH Daily Options | 0% |
| BTC/ETH Options | 0.015%, never higher than 12.5% of the option's value |

Daily options are exempt. The instrument record's `settlement_period` (`day` / `week` / `month`)
tells which rule applies, and it is stored in the raw data with every `get_instruments` response.
Fee discounts do not apply to delivery fees.

## Other facts from the page
- Option tick size (API): 0.0001, or 0.0005 above a price of 0.005 BTC (`tick_size_steps`).
- Out of scope for this project: VIP tiers, block trades, combos, liquidation fees.
