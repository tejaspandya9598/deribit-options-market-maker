#include "quoter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "pricing.hpp"

namespace mm {

namespace {

constexpr double kEps = 1e-9;

// Tick in 1e-8 units at a price: the base tick, or the step whose above_price the price exceeds.
Px tick_at(const InstrumentDef& d, double price) {
    double tick = d.tick_size;
    for (const auto& [above, step] : d.tick_steps) {
        if (price > above) tick = step;
    }
    return to_e8(tick);
}

double years_to(Nanos expiry, Nanos now) {
    return static_cast<double>(expiry - now) / static_cast<double>(kYear);
}

}  // namespace

Quoter::Quoter(EventLoop& loop, std::uint32_t account, std::unique_ptr<Strategy> strategy,
               QuoterConfig cfg, const FeeSchedule* fees)
    : loop_(loop), account_(account), strategy_(std::move(strategy)), cfg_(cfg), fees_(fees) {}

void Quoter::on_index(double price, Nanos t) {
    if (!(price > 0)) return;
    if (rv_last_price_ > 0 && t > rv_last_time_) {
        const double dt = static_cast<double>(t - rv_last_time_) / kSec;
        const double r = std::log(price / rv_last_price_);
        // Weight of the new observation for a half-life of rv_halflife.
        const double w = 1.0 - std::exp(-dt * std::numbers::ln2 / (static_cast<double>(cfg_.rv_halflife) / kSec));
        rv_var_rate_ += w * (r * r / dt - rv_var_rate_);
    } else if (!(rv_last_price_ > 0)) {
        rv_first_time_ = t;
    }
    if (t >= rv_last_time_) {
        rv_last_price_ = price;
        rv_last_time_ = t;
    }
}

double Quoter::realized_vol() const {
    if (rv_first_time_ == 0 || rv_last_time_ - rv_first_time_ < cfg_.rv_halflife) return kNaN;
    return std::sqrt(rv_var_rate_ * static_cast<double>(kYear / kSec));
}

Quoter::State& Quoter::state(InstrumentId id) {
    auto i = static_cast<size_t>(id);
    if (i >= states_.size()) states_.resize(i + 1);
    return states_[i];
}

double Quoter::position(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return id >= 0 && i < states_.size() ? states_[i].position : 0.0;
}

void Quoter::on_market(const Event& e, const MarketState& view, Nanos now) {
    if (const auto* k = std::get_if<Ticker>(&e.payload)) {
        state(k->id).last_ticker = e.time;
        maybe_evaluate(k->id, view, now);
    } else if (const auto* u = std::get_if<BookUpdate>(&e.payload)) {
        maybe_evaluate(u->id, view, now);
    } else if (const auto* x = std::get_if<IndexPrice>(&e.payload)) {
        on_index(x->price, e.time);
    }
    // Quotes on an option that went quiet get no event to pull them on: check once a second.
    if (now >= next_sweep_) {
        sweep(view, now);
        next_sweep_ = now + kSec;
    }
}

void Quoter::maybe_evaluate(InstrumentId id, const MarketState& view, Nanos now) {
    State& st = state(id);
    if (now - st.last_eval < cfg_.requote_interval) return;
    const InstrumentDef* d = view.def(id);
    if (d == nullptr || d->kind != InstrumentKind::Option) return;
    evaluate(id, st, view, now);
}

void Quoter::sweep(const MarketState& view, Nanos now) {
    update_vega(view, now);
    for (size_t i = 0; i < states_.size(); ++i) {
        State& st = states_[i];
        if (st.bid.state != SlotState::Live && st.ask.state != SlotState::Live) continue;
        if (now - st.last_eval < cfg_.requote_interval) continue;
        evaluate(static_cast<InstrumentId>(i), st, view, now);
    }
}

void Quoter::update_vega(const MarketState& view, Nanos now) {
    double vega = 0;
    for (size_t i = 0; i < states_.size(); ++i) {
        const double pos = states_[i].position;
        if (std::abs(pos) <= kEps) continue;
        const auto id = static_cast<InstrumentId>(i);
        const InstrumentDef* d = view.def(id);
        const Ticker* k = view.ticker(id);
        if (d == nullptr || k == nullptr || d->kind != InstrumentKind::Option) continue;
        const double T = years_to(d->expiry, now);
        if (!(T > 0) || !(k->underlying_price > 0) || !(k->mark_iv > 0)) continue;
        vega += pos * pricing::black76(k->underlying_price, d->strike, T, k->mark_iv / 100.0, d->is_call).vega / 100.0;
    }
    vega_ = vega;
}

void Quoter::evaluate(InstrumentId id, State& st, const MarketState& view, Nanos now) {
    st.last_eval = now;
    ++stats_.evaluations;
    Want bid, ask;
    desired(id, st, view, now, bid, ask);
    const InstrumentDef* d = view.def(id);
    reconcile(id, Side::Buy, st, st.bid, bid, d != nullptr ? tick_at(*d, from_e8(st.bid.price)) : 0);
    reconcile(id, Side::Sell, st, st.ask, ask, d != nullptr ? tick_at(*d, from_e8(st.ask.price)) : 0);
}

void Quoter::desired(InstrumentId id, const State& st, const MarketState& view, Nanos now,
                     Want& want_bid, Want& want_ask) {
    std::optional<Px>& bid = want_bid.price;
    std::optional<Px>& ask = want_ask.price;
    const InstrumentDef* d = view.def(id);
    if (d == nullptr || d->kind != InstrumentKind::Option) return;
    if (cfg_.trade_from != 0 && now < cfg_.trade_from) return;
    if (cfg_.trade_until != 0 && now >= cfg_.trade_until) return;
    if (now >= d->expiry - cfg_.no_quote_before_expiry) return;
    const bool reduce_only = now >= d->expiry - cfg_.reduce_only_before_expiry;
    if (reduce_only && std::abs(st.position) <= kEps) return;
    const OrderBook* book = view.book(id);
    const Ticker* k = view.ticker(id);
    if (book == nullptr || k == nullptr || !book->valid()) return;
    if (now - std::max(book->last_update(), st.last_ticker) > cfg_.stale_after) return;
    if (!(k->underlying_price > 0) || !(k->mark_iv > 0)) return;
    const double abs_delta = std::abs(k->delta);
    if (!(abs_delta >= cfg_.min_abs_delta && abs_delta <= cfg_.max_abs_delta)) return;

    QuoteContext ctx;
    ctx.now = now;
    ctx.def = d;
    ctx.book = book;
    ctx.ticker = k;
    ctx.forward = k->underlying_price;
    ctx.t_years = years_to(d->expiry, now);
    ctx.mark_iv = k->mark_iv / 100.0;
    ctx.position = st.position;
    ctx.account_vega = vega_;
    ctx.max_vega = cfg_.max_vega;
    ctx.realized_vol = realized_vol();
    ctx.fee_edge_mult = cfg_.fee_edge_mult;
    ctx.fees = fees_;
    if (auto it = view.fits().find(d->expiry); it != view.fits().end()) {
        const SabrFit& f = it->second;
        if (now - (f.fit_time + f.compute_ns) <= cfg_.fit_max_age) ctx.fit = &f;
    }
    Quote q = strategy_->quote(ctx);

    // Fee floor: at least min_edge between the quote and fair, on both sides.
    if (q.fair && std::isfinite(*q.fair) && *q.fair > 0) {
        const double edge = ctx.min_edge(*q.fair);
        if (edge > 0) {
            const double cap = *q.fair - edge, floor = *q.fair + edge;
            if (q.bid) q.bid = std::min(*q.bid, cap);
            if (q.ask) q.ask = std::max(*q.ask, floor);
            // The same bounds on the tick grid, for quotes the re-quote band keeps.
            if (cap > 0) want_bid.bound = static_cast<Px>(std::floor(cap * kE8 / static_cast<double>(tick_at(*d, cap)) + kEps)) * tick_at(*d, cap);
            want_ask.bound = static_cast<Px>(std::ceil(floor * kE8 / static_cast<double>(tick_at(*d, floor)) - kEps)) * tick_at(*d, floor);
        }
    }

    const Px min_tick = to_e8(d->tick_size);
    const auto best_bid = book->best_bid();
    const auto best_ask = book->best_ask();
    if (q.bid && std::isfinite(*q.bid) && *q.bid > 0) {
        const Px tick = tick_at(*d, *q.bid);
        Px px = static_cast<Px>(std::floor(*q.bid * kE8 / static_cast<double>(tick) + kEps)) * tick;
        if (best_ask && px >= best_ask->price) px = best_ask->price - tick_at(*d, from_e8(best_ask->price) - 1e-9);
        if (px >= min_tick) bid = px;
    }
    if (q.ask && std::isfinite(*q.ask) && *q.ask > 0) {
        const Px tick = tick_at(*d, *q.ask);
        Px px = static_cast<Px>(std::ceil(*q.ask * kE8 / static_cast<double>(tick) - kEps)) * tick;
        if (best_bid && px <= best_bid->price) px = best_bid->price + tick_at(*d, from_e8(best_bid->price) + 1e-9);
        if (px >= min_tick) ask = px;
    }
    if (bid && ask && *bid >= *ask) {  // can only come from a strategy quoting bid >= ask
        bid.reset();
        ask.reset();
    }

    // Risk limits: stop adding on the side that would take us further out.
    if (st.position + cfg_.size > cfg_.max_position + kEps || vega_ >= cfg_.max_vega) bid.reset();
    if (st.position - cfg_.size < -cfg_.max_position - kEps || vega_ <= -cfg_.max_vega) ask.reset();
    // Close to expiry: only the side that takes the position towards zero.
    if (reduce_only) (st.position > 0 ? bid : ask).reset();
}

void Quoter::reconcile(InstrumentId id, Side side, State& st, Slot& slot, const Want& w, Px tick) {
    const std::optional<Px>& want = w.price;
    OrderRequest o;
    o.account = account_;
    o.instrument = id;
    o.side = side;
    o.type = OrderType::PostOnly;
    switch (slot.state) {
        case SlotState::None:
            if (!want) return;
            o.action = OrderAction::Place;
            o.client_id = next_id_++;
            o.price = *want;
            o.amount = cfg_.size;
            slot = {SlotState::Pending, o.client_id, o.price, o.amount};
            ++stats_.places;
            break;
        case SlotState::Live:
            if (!want) {
                o.action = OrderAction::Cancel;
                o.client_id = slot.id;
                slot.state = SlotState::Cancelling;
                ++stats_.cancels;
            } else if (*want != slot.price) {
                // Re-quote band: a small move is not worth the queue place, as long as the
                // resting quote still keeps the fee floor.
                const bool floor_ok = w.bound == 0 || (side == Side::Buy ? slot.price <= w.bound : slot.price >= w.bound);
                const Px gap = *want > slot.price ? *want - slot.price : slot.price - *want;
                if (cfg_.requote_band_ticks > 0 && floor_ok && gap <= cfg_.requote_band_ticks * tick) return;
                o.action = OrderAction::Replace;
                o.client_id = slot.id;
                o.new_id = next_id_++;
                o.price = *want;
                o.amount = cfg_.size;
                slot = {SlotState::Pending, o.new_id, o.price, o.amount};
                ++stats_.replaces;
            } else {
                return;
            }
            break;
        case SlotState::Pending:
        case SlotState::Cancelling:
            return;  // wait for the exchange's answer
    }
    if (!st.quoted && o.action != OrderAction::Cancel) {
        st.quoted = true;
        stats_.quoted.push_back(id);
    }
    loop_.submit_order(o);
}

void Quoter::on_order_update(const OrderUpdate& u, const MarketState& view, Nanos now) {
    if (u.account != account_ || u.instrument < 0) return;
    State& st = state(u.instrument);
    if (u.kind == UpdateKind::Settled) {
        st.position = 0;
        update_vega(view, now);
        return;
    }
    if (u.kind == UpdateKind::Fill) {
        st.position += sign(u.side) * u.amount;
        if (std::abs(st.position) <= kEps) st.position = 0;
        update_vega(view, now);
    }

    // Updates for orders we already replaced or gave up on only matter for the position.
    Slot* slot = nullptr;
    if (st.bid.state != SlotState::None && st.bid.id == u.client_id) slot = &st.bid;
    else if (st.ask.state != SlotState::None && st.ask.id == u.client_id) slot = &st.ask;
    if (slot == nullptr) return;

    switch (u.kind) {
        case UpdateKind::Accepted:
            slot->state = SlotState::Live;
            slot->price = u.price;
            slot->remaining = u.remaining;
            break;
        case UpdateKind::Fill:
            slot->remaining = u.remaining;
            if (slot->remaining <= kEps) *slot = {};
            break;
        case UpdateKind::Rejected:
        case UpdateKind::Cancelled:
            *slot = {};
            break;
        case UpdateKind::Settled: break;
    }
}

}  // namespace mm
