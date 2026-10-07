#include "strategy.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "pricing.hpp"

namespace mm {

double QuoteContext::sabr_iv() const {
    if (fit == nullptr || !(forward > 0) || !(t_years > 0)) return kNaN;
    try {
        const double v = pricing::hagan_lognormal_vol(forward, def->strike, t_years, fit->alpha,
                                                      fit->beta, fit->rho, fit->nu);
        return std::isfinite(v) && v > 0 ? v : kNaN;
    } catch (const std::invalid_argument&) {
        return kNaN;
    }
}

double QuoteContext::price_btc(double iv) const {
    return pricing::to_btc(pricing::black76_price(forward, def->strike, t_years, iv, def->is_call), forward);
}

double QuoteContext::iv_of(double price_btc) const {
    const auto r = pricing::implied_vol(price_btc * forward, forward, def->strike, t_years, def->is_call);
    return r.status == pricing::IvStatus::Ok ? r.vol : kNaN;
}

double QuoteContext::min_edge(double fair_btc) const {
    if (fees == nullptr || !(fee_edge_mult > 0) || !(fair_btc > 0)) return 0.0;
    return fee_edge_mult * fees->option_fee(fair_btc, 1.0, /*maker=*/true);
}

Quote iv_spread_quote(const QuoteContext& ctx, double centre_iv, double half_spread) {
    Quote q;
    if (!std::isfinite(centre_iv) || !(centre_iv > 0)) return q;
    if (centre_iv - half_spread > 0) q.bid = ctx.price_btc(centre_iv - half_spread);
    q.ask = ctx.price_btc(centre_iv + half_spread);
    q.fair = ctx.price_btc(centre_iv);
    return q;
}

namespace {

std::map<std::string, StrategyFactory>& registry() {
    static std::map<std::string, StrategyFactory> r;
    return r;
}

}  // namespace

void register_strategy(const std::string& name, StrategyFactory factory) {
    registry()[name] = std::move(factory);
}

void register_builtin_strategies() {
    register_strategy("fixed_spread", make_fixed_spread);
    register_strategy("mark_iv_follower", make_mark_iv_follower);
    register_strategy("inventory_skew", make_inventory_skew);
    register_strategy("join_best", make_join_best);
}

std::unique_ptr<Strategy> make_strategy(const std::string& name, const nlohmann::json& params) {
    auto it = registry().find(name);
    if (it == registry().end()) throw std::invalid_argument("unknown strategy '" + name + "'");
    return it->second(params);
}

std::vector<std::string> strategy_names() {
    std::vector<std::string> names;
    for (const auto& [name, f] : registry()) names.push_back(name);
    return names;
}

double param(const nlohmann::json& params, const char* key, double fallback) {
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    if (!it->is_number()) throw std::invalid_argument(std::string(key) + " must be a number");
    return it->get<double>();
}

void check_keys(const nlohmann::json& params, std::initializer_list<const char*> known,
                const std::string& where) {
    if (params.is_null()) return;
    if (!params.is_object()) throw std::invalid_argument(where + " must be an object");
    for (const auto& [key, value] : params.items()) {
        const bool ok = std::any_of(known.begin(), known.end(), [&](const char* k) { return key == k; });
        if (!ok) throw std::invalid_argument("unknown key '" + key + "' in " + where);
    }
}

}  // namespace mm
