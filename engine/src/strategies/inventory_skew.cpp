// Inventory-skewed quotes (Avellaneda-Stoikov style), in IV space:
//   centre      = SABR fair IV - skew x account vega        (leans against the vega we hold)
//   half-spread = base
//                 + inventory_widen x |account vega| / vega limit
//                 + vol_widen x (realized vol - fair IV, if positive), capped at max_vol_widen
// All spreads in vol points; vega in USD per vol point; skew in vol points per 100 of vega.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "strategy.hpp"

namespace mm {

namespace {

class InventorySkew final : public Strategy {
public:
    InventorySkew(double half, double skew, double inv_widen, double vol_widen, double max_vol_widen)
        : half_(half), skew_(skew), inv_widen_(inv_widen), vol_widen_(vol_widen), max_vol_widen_(max_vol_widen) {}

    Quote quote(const QuoteContext& ctx) override {
        const double fair = ctx.sabr_iv();
        if (!std::isfinite(fair)) return {};
        const double centre_vp = fair * 100.0 - skew_ * ctx.account_vega / 100.0;
        double half_vp = half_;
        if (ctx.max_vega > 0) half_vp += inv_widen_ * std::min(1.0, std::abs(ctx.account_vega) / ctx.max_vega);
        if (std::isfinite(ctx.realized_vol) && ctx.realized_vol > fair) {
            half_vp += std::min(max_vol_widen_, vol_widen_ * (ctx.realized_vol - fair) * 100.0);
        }
        Quote q = iv_spread_quote(ctx, centre_vp / 100.0, half_vp / 100.0);
        // The fee floor is measured from fair value, not from the skewed centre.
        if (q.fair) q.fair = ctx.price_btc(fair);
        return q;
    }
    std::string fair_value_source() const override { return "sabr"; }
    nlohmann::json params() const override {
        return {{"half_spread_vp", half_},
                {"skew_vp_per_100_vega", skew_},
                {"inventory_widen_vp", inv_widen_},
                {"vol_widen", vol_widen_},
                {"max_vol_widen_vp", max_vol_widen_}};
    }

private:
    double half_, skew_, inv_widen_, vol_widen_, max_vol_widen_;
};

}  // namespace

std::unique_ptr<Strategy> make_inventory_skew(const nlohmann::json& params) {
    check_keys(params, {"half_spread_vp", "skew_vp_per_100_vega", "inventory_widen_vp", "vol_widen", "max_vol_widen_vp"},
               "inventory_skew params");
    const double half = param(params, "half_spread_vp", 2.0);
    const double skew = param(params, "skew_vp_per_100_vega", 1.0);
    const double inv = param(params, "inventory_widen_vp", 1.0);
    const double vol = param(params, "vol_widen", 0.25);
    const double cap = param(params, "max_vol_widen_vp", 5.0);
    if (!(half > 0)) throw std::invalid_argument("inventory_skew: half_spread_vp must be > 0");
    if (skew < 0 || inv < 0 || vol < 0 || cap < 0) {
        throw std::invalid_argument("inventory_skew: skew and widening parameters must be >= 0");
    }
    return std::make_unique<InventorySkew>(half, skew, inv, vol, cap);
}

}  // namespace mm
