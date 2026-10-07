// Strategy interface and registry.
//
// A strategy answers one question: for this option, right now, what bid and ask would I like?
// Everything shared lives in the Quoter (quoter.hpp): which options are quoted, tick rounding,
// risk limits, pulling quotes on stale data, and turning quotes into place / cancel / replace.
//
// To add a strategy: derive from Strategy, then register_strategy("name", factory) before the
// run config is loaded (built-in ones are registered by register_builtin_strategies()).
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "events.hpp"
#include "order_book.hpp"

namespace mm {

// Everything here comes from the strategy view (market data after latency) and from fills
// already reported back, never from the exchange-side state.
struct QuoteContext {
    Nanos now = 0;
    const InstrumentDef* def = nullptr;
    const OrderBook* book = nullptr;
    const Ticker* ticker = nullptr;
    const SabrFit* fit = nullptr;  // latest fit for this expiry; nullptr if none or too old
    double forward = kNaN;         // the ticker's underlying price for this expiry
    double t_years = kNaN;
    double mark_iv = kNaN;      // Deribit mark IV, as a decimal (0.45 = 45 vol points)
    double position = 0;        // contracts in this option
    double account_vega = 0;    // USD per vol point, whole account

    // SABR fair IV (decimal): Hagan's formula with the fit's parameters at the current forward.
    // NaN without a usable fit.
    double sabr_iv() const;
    // Black-76 premium in BTC for an IV (decimal).
    double price_btc(double iv) const;
};

// Prices in BTC, not yet rounded: the Quoter rounds bids down and asks up to the tick.
// An empty side means "no quote on that side".
struct Quote {
    std::optional<double> bid, ask;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual Quote quote(const QuoteContext& ctx) = 0;
    // What the quotes are centred on, for the results table (e.g. "sabr", "deribit_mark_iv").
    virtual std::string fair_value_source() const = 0;
    // Parameters actually in use, defaults included, for config.json.
    virtual nlohmann::json params() const = 0;
};

// Throws std::invalid_argument on a bad or unknown parameter.
using StrategyFactory = std::function<std::unique_ptr<Strategy>(const nlohmann::json& params)>;

void register_strategy(const std::string& name, StrategyFactory factory);
void register_builtin_strategies();
// Throws std::invalid_argument for an unknown name.
std::unique_ptr<Strategy> make_strategy(const std::string& name, const nlohmann::json& params);
std::vector<std::string> strategy_names();

// For factories: read params[key] (default if absent) and reject keys not in `known`.
double param(const nlohmann::json& params, const char* key, double fallback);
void check_keys(const nlohmann::json& params, std::initializer_list<const char*> known,
                const std::string& where);

// Bid and ask at centre_iv -/+ half_spread (both decimals), priced with Black-76. No quote at
// all without a centre; no bid if its IV would not be positive.
Quote iv_spread_quote(const QuoteContext& ctx, double centre_iv, double half_spread);

// Built-in factories (strategies/*.cpp).
std::unique_ptr<Strategy> make_fixed_spread(const nlohmann::json& params);
std::unique_ptr<Strategy> make_mark_iv_follower(const nlohmann::json& params);

}  // namespace mm
