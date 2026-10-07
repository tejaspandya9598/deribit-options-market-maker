// Hedger interface and registry. A hedger answers one question: given the account's delta,
// how much of it should be traded away on BTC-PERPETUAL right now?
//
// The HedgeRunner (hedge_runner.hpp) does the rest: it works out the delta from the strategy
// view, sizes the perpetual order in whole lots and sends it as an immediate-or-cancel taker.
//
//   none             never hedges
//   fixed_band       back to zero whenever |delta| is above band_btc
//   time_based       back to zero every interval_min minutes (on the clock: 00:00, 00:15, ...);
//                    never before the first such time after it starts
//   whalley_wilmott  no-trade band from gamma, cost and risk aversion (Whalley-Wilmott 1997):
//                      half-width (BTC of delta) = (1.5 x cost x S x gamma^2 / risk_aversion)^(1/3)
//                    never below min_band_btc; outside it, trades back to the band's edge.
//                    cost = fraction of notional paid per hedge (the perpetual taker fee),
//                    S = perpetual price, gamma = the options' gamma per USD, risk_aversion per
//                    USD. Wider when trading costs more, narrower when you care more about risk.
//
// To add a hedger: derive from Hedger and register_hedger("name", factory).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "types.hpp"

namespace mm {

struct HedgeContext {
    Nanos now = 0;
    double delta = 0;       // BTC, whole account including the perpetual
    double gamma = 0;       // options, per USD
    double perp_price = 0;  // USD
    double cost_rate = 0;   // taker fee as a fraction of notional
};

class Hedger {
public:
    virtual ~Hedger() = default;
    // The change in account delta to trade now, in BTC (negative = sell delta). 0 = do nothing.
    virtual double trade_delta(const HedgeContext& ctx) = 0;
    // Parameters actually in use, defaults included, for config.json.
    virtual nlohmann::json params() const = 0;
};

// Throws std::invalid_argument on a bad or unknown parameter.
using HedgerFactory = std::function<std::unique_ptr<Hedger>(const nlohmann::json& params)>;

void register_hedger(const std::string& name, HedgerFactory factory);
void register_builtin_hedgers();
// Throws std::invalid_argument for an unknown name.
std::unique_ptr<Hedger> make_hedger(const std::string& name, const nlohmann::json& params);

// The Whalley-Wilmott half-width above, before the min_band_btc floor.
double whalley_wilmott_band(double cost_rate, double perp_price, double gamma, double risk_aversion);

}  // namespace mm
