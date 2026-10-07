// Fixed spread: SABR fair IV -/+ a fixed half-spread in vol points.
#include <stdexcept>

#include "strategy.hpp"

namespace mm {

namespace {

class FixedSpread final : public Strategy {
public:
    explicit FixedSpread(double half_spread_vp) : half_spread_vp_(half_spread_vp) {}
    Quote quote(const QuoteContext& ctx) override {
        return iv_spread_quote(ctx, ctx.sabr_iv(), half_spread_vp_ / 100.0);
    }
    std::string fair_value_source() const override { return "sabr"; }
    nlohmann::json params() const override { return {{"half_spread_vp", half_spread_vp_}}; }

private:
    double half_spread_vp_;
};

}  // namespace

std::unique_ptr<Strategy> make_fixed_spread(const nlohmann::json& params) {
    check_keys(params, {"half_spread_vp"}, "fixed_spread params");
    const double hs = param(params, "half_spread_vp", 2.0);
    if (!(hs > 0)) throw std::invalid_argument("fixed_spread: half_spread_vp must be > 0");
    return std::make_unique<FixedSpread>(hs);
}

}  // namespace mm
