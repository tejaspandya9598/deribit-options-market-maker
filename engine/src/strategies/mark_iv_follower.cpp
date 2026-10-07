// Mark-IV follower: the same fixed spread, centred on Deribit's mark IV instead of SABR. Run
// next to fixed_spread, it turns the choice of fair value into a strategy comparison.
#include <stdexcept>

#include "strategy.hpp"

namespace mm {

namespace {

class MarkIvFollower final : public Strategy {
public:
    explicit MarkIvFollower(double half_spread_vp) : half_spread_vp_(half_spread_vp) {}
    Quote quote(const QuoteContext& ctx) override {
        return iv_spread_quote(ctx, ctx.mark_iv, half_spread_vp_ / 100.0);
    }
    std::string fair_value_source() const override { return "deribit_mark_iv"; }
    nlohmann::json params() const override { return {{"half_spread_vp", half_spread_vp_}}; }

private:
    double half_spread_vp_;
};

}  // namespace

std::unique_ptr<Strategy> make_mark_iv_follower(const nlohmann::json& params) {
    check_keys(params, {"half_spread_vp"}, "mark_iv_follower params");
    const double hs = param(params, "half_spread_vp", 2.0);
    if (!(hs > 0)) throw std::invalid_argument("mark_iv_follower: half_spread_vp must be > 0");
    return std::make_unique<MarkIvFollower>(hs);
}

}  // namespace mm
