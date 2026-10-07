// Join the best with an edge filter: quote at the current best bid / best ask, each side only
// when that price is at least min_edge_vp vol points on the right side of SABR fair value (and
// outside the quoter's fee floor). Otherwise stay out on that side.
#include <cmath>
#include <stdexcept>

#include "strategy.hpp"

namespace mm {

namespace {

class JoinBest final : public Strategy {
public:
    explicit JoinBest(double min_edge_vp) : min_edge_vp_(min_edge_vp) {}

    Quote quote(const QuoteContext& ctx) override {
        Quote q;
        const double fair_iv = ctx.sabr_iv();
        if (!std::isfinite(fair_iv)) return q;
        const double fair = ctx.price_btc(fair_iv);
        const double floor = ctx.min_edge(fair);
        q.fair = fair;
        if (auto b = ctx.book->best_bid()) {
            const double px = from_e8(b->price);
            const double iv = ctx.iv_of(px);
            if (std::isfinite(iv) && (fair_iv - iv) * 100.0 >= min_edge_vp_ && fair - px >= floor) q.bid = px;
        }
        if (auto a = ctx.book->best_ask()) {
            const double px = from_e8(a->price);
            const double iv = ctx.iv_of(px);
            if (std::isfinite(iv) && (iv - fair_iv) * 100.0 >= min_edge_vp_ && px - fair >= floor) q.ask = px;
        }
        return q;
    }
    std::string fair_value_source() const override { return "sabr"; }
    nlohmann::json params() const override { return {{"min_edge_vp", min_edge_vp_}}; }

private:
    double min_edge_vp_;
};

}  // namespace

std::unique_ptr<Strategy> make_join_best(const nlohmann::json& params) {
    check_keys(params, {"min_edge_vp"}, "join_best params");
    const double edge = param(params, "min_edge_vp", 2.0);
    if (edge < 0) throw std::invalid_argument("join_best: min_edge_vp must be >= 0");
    return std::make_unique<JoinBest>(edge);
}

}  // namespace mm
