#include "hedge_runner.hpp"

#include <cmath>

namespace mm {

namespace {
constexpr double kEps = 1e-9;
}

HedgeRunner::HedgeRunner(EventLoop& loop, std::uint32_t account, std::unique_ptr<Hedger> hedger,
                         const FeeSchedule& fees, HedgeRunnerConfig cfg)
    : loop_(loop), account_(account), hedger_(std::move(hedger)), fees_(fees), cfg_(cfg) {}

void HedgeRunner::on_market(const Event& e, const MarketState& view, Nanos now) {
    InstrumentId id = -1;
    if (const auto* k = std::get_if<Ticker>(&e.payload)) id = k->id;
    else if (const auto* u = std::get_if<BookUpdate>(&e.payload)) id = u->id;
    else if (const auto* d = std::get_if<InstrumentDef>(&e.payload)) {
        if (d->kind == InstrumentKind::Perpetual) perp_ = d->id;
        return;
    }
    if (id < 0 || id != perp_ || now - last_check_ < cfg_.interval) return;
    last_check_ = now;
    check(view, now);
}

void HedgeRunner::check(const MarketState& view, Nanos now) {
    if (pending_ != 0) {
        // An answer normally takes one trip each way. If none came in well over that, the
        // order is given up on; its fills, should they still arrive, still count (in the
        // portfolio and in the fill statistics).
        const Nanos wait = cfg_.answer_timeout + 2 * (loop_.config().md_latency + loop_.config().order_latency);
        if (now - pending_since_ < wait) return;
        ++stats_.timed_out;
        pending_ = 0;
    }
    if (cfg_.trade_from != 0 && now < cfg_.trade_from) return;
    if (cfg_.trade_until != 0 && now >= cfg_.trade_until) return;
    const InstrumentDef* d = view.def(perp_);
    const OrderBook* book = view.book(perp_);
    if (d == nullptr || book == nullptr || !book->valid()) return;
    ++stats_.checks;

    const PortfolioGreeks g = portfolio_.greeks(view, perp_, now);
    if (!(g.perp_price > 0)) return;
    HedgeContext ctx;
    ctx.now = now;
    ctx.delta = g.delta;
    ctx.gamma = g.gamma;
    ctx.perp_price = g.perp_price;
    ctx.cost_rate = fees_.perp_taker;
    const double want = hedger_->trade_delta(ctx);
    if (want == 0 || !std::isfinite(want)) return;

    // A perpetual position of A USD carries A / price BTC of delta.
    const double lot = d->min_trade_amount > 0 ? d->min_trade_amount : 10.0;
    const double usd = std::round(std::abs(want) * g.perp_price / lot) * lot;
    if (usd < lot - kEps) return;

    OrderRequest o;
    o.account = account_;
    o.client_id = next_id_++;
    o.action = OrderAction::Place;
    o.type = OrderType::Ioc;
    o.instrument = perp_;
    o.side = want > 0 ? Side::Buy : Side::Sell;
    o.amount = usd;
    const Px tick = to_e8(d->tick_size);
    if (o.side == Side::Buy) {
        const auto ask = book->best_ask();
        if (!ask) return;
        // Rounded towards the best price, so the limit never allows more than `slippage`.
        o.price = static_cast<Px>(std::floor(static_cast<double>(ask->price) * (1.0 + cfg_.slippage) / static_cast<double>(tick))) * tick;
    } else {
        const auto bid = book->best_bid();
        if (!bid) return;
        o.price = static_cast<Px>(std::ceil(static_cast<double>(bid->price) * (1.0 - cfg_.slippage) / static_cast<double>(tick))) * tick;
    }
    pending_ = o.client_id;
    pending_since_ = now;
    ++stats_.orders;
    loop_.submit_order(o);
}

void HedgeRunner::on_order_update(const OrderUpdate& u, const MarketState&, Nanos) {
    if (u.account != account_ || u.instrument < 0) return;
    // Every fill of the account counts: the quoter's option fills and our own hedges.
    if (u.kind == UpdateKind::Fill) portfolio_.add(u.instrument, sign(u.side) * u.amount);
    else if (u.kind == UpdateKind::Settled) portfolio_.clear(u.instrument);

    // Fills of any hedge order of ours count, also of one already given up on.
    const bool ours = u.client_id >= kFirstHedgeId && u.client_id < next_id_;
    if (u.kind == UpdateKind::Fill && ours) ++stats_.fills;

    if (pending_ == 0 || u.client_id != pending_) return;
    switch (u.kind) {
        case UpdateKind::Fill:
            if (u.remaining <= kEps) pending_ = 0;
            break;
        case UpdateKind::Cancelled:  // the part the book could not fill inside the limit
            ++stats_.unfilled;
            pending_ = 0;
            break;
        case UpdateKind::Rejected:
            ++stats_.rejected;
            pending_ = 0;
            break;
        default: break;
    }
}

}  // namespace mm
