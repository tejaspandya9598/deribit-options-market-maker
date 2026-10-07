#include "portfolio.hpp"

#include <cmath>

#include "pricing.hpp"

namespace mm {

namespace {
constexpr double kEps = 1e-9;
}

void Portfolio::add(InstrumentId id, double signed_amount) {
    auto i = static_cast<size_t>(id);
    if (i >= pos_.size()) pos_.resize(i + 1);
    pos_[i] += signed_amount;
    if (std::abs(pos_[i]) <= kEps) pos_[i] = 0;
}

void Portfolio::clear(InstrumentId id) {
    auto i = static_cast<size_t>(id);
    if (i < pos_.size()) pos_[i] = 0;
}

double Portfolio::position(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return id >= 0 && i < pos_.size() ? pos_[i] : 0.0;
}

double mark_price(const MarketState& state, InstrumentId id) {
    const OrderBook* b = state.book(id);
    if (b != nullptr && b->valid() && !b->bids().empty() && !b->asks().empty()) {
        return from_e8(b->bids().begin()->first + b->asks().begin()->first) / 2.0;
    }
    const Ticker* k = state.ticker(id);
    return k != nullptr && k->mark_price > 0 ? k->mark_price : kNaN;
}

PortfolioGreeks Portfolio::greeks(const MarketState& view, InstrumentId perp, Nanos now) const {
    PortfolioGreeks g;
    if (perp >= 0) g.perp_price = mark_price(view, perp);
    for (size_t i = 0; i < pos_.size(); ++i) {
        const double qty = pos_[i];
        if (qty == 0) continue;
        const auto id = static_cast<InstrumentId>(i);
        if (id == perp) {
            if (g.perp_price > 0) g.delta += qty / g.perp_price;
            continue;
        }
        const InstrumentDef* d = view.def(id);
        const Ticker* k = view.ticker(id);
        if (d == nullptr || k == nullptr || d->kind != InstrumentKind::Option) continue;
        const double T = static_cast<double>(d->expiry - now) / static_cast<double>(kYear);
        if (!(T > 0) || !(k->underlying_price > 0) || !(k->mark_iv > 0)) continue;
        const auto b = pricing::black76(k->underlying_price, d->strike, T, k->mark_iv / 100.0, d->is_call);
        g.delta += qty * pricing::inverse_delta(b.delta, pricing::to_btc(b.price, k->underlying_price));
        g.gamma += qty * b.gamma;
        g.vega += qty * b.vega / 100.0;
    }
    return g;
}

}  // namespace mm
