#include "exchange_sim.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "pricing.hpp"

namespace mm {

namespace {

constexpr double kEps = 1e-9;

double level_size(const OrderBook& b, Side side, Px price) {
    if (side == Side::Buy) {
        auto it = b.bids().find(price);
        return it == b.bids().end() ? 0.0 : it->second;
    }
    auto it = b.asks().find(price);
    return it == b.asks().end() ? 0.0 : it->second;
}

// Mid of a valid two-sided book, as quoted; NaN otherwise.
double book_mid(const OrderBook* b) {
    if (b == nullptr || !b->valid() || b->bids().empty() || b->asks().empty()) return kNaN;
    return from_e8(b->bids().begin()->first + b->asks().begin()->first) / 2.0;
}

// Value of one option contract at delivery price S, in BTC.
double intrinsic_btc(const InstrumentDef& d, double S) {
    if (!(S > 0)) return kNaN;
    return (d.is_call ? std::max(S - d.strike, 0.0) : std::max(d.strike - S, 0.0)) / S;
}

}  // namespace

ExchangeSim::ExchangeSim(EventLoop& loop, const FeeSchedule& fees, SimConfig cfg,
                         const std::vector<std::string>& account_names, RunSink* sink)
    : loop_(loop),
      fees_(fees),
      cfg_(cfg),
      model_(make_fill_model(cfg.fill_model, cfg.trade_through, cfg.late_window)),
      sink_(sink) {
    if (cfg_.pnl_interval <= 0) throw std::invalid_argument("ExchangeSim: pnl_interval must be > 0");
    for (const auto& n : account_names) accounts_.emplace_back(n, cfg_.starting_equity);
    stats_.resize(accounts_.size());
    rows_.resize(accounts_.size());
}

ExchangeSim::Instr& ExchangeSim::instr(InstrumentId id) {
    auto i = static_cast<size_t>(id);
    if (i >= instr_.size()) instr_.resize(i + 1);
    return instr_[i];
}

std::vector<Resting>& ExchangeSim::orders(InstrumentId id) {
    auto i = static_cast<size_t>(id);
    if (i >= orders_.size()) orders_.resize(i + 1);
    return orders_[i];
}

const std::vector<Resting>& ExchangeSim::resting(InstrumentId id) const {
    static const std::vector<Resting> none;
    auto i = static_cast<size_t>(id);
    return id >= 0 && i < orders_.size() ? orders_[i] : none;
}

double ExchangeSim::unit_value(InstrumentId id, const MarketState& exch) {
    Instr& s = instr(id);
    if (s.expired) return s.frozen_u;
    const InstrumentDef* d = exch.def(id);
    if (d == nullptr) return s.last_u;
    double px = book_mid(exch.book(id));
    if (!(px > 0)) {
        if (const Ticker* k = exch.ticker(id); k != nullptr && k->mark_price > 0) px = k->mark_price;
    }
    if (px > 0) s.last_u = d->kind == InstrumentKind::Option ? px : -1.0 / px;
    return s.last_u;
}

void ExchangeSim::send(const OrderRequest& o, std::uint64_t id, UpdateKind kind, Reject reason,
                       Px price, double amount, double remaining, Nanos t) {
    if (o.account < stats_.size()) {
        AccountStats& st = stats_[o.account];
        if (kind == UpdateKind::Rejected) {
            ++(reason == Reject::WouldCross ? st.rejected_cross : st.rejected_other);
        } else if (kind == UpdateKind::Cancelled) {
            ++st.cancelled;
        }
    }
    OrderUpdate u;
    u.account = o.account;
    u.client_id = id;
    u.kind = kind;
    u.reason = reason;
    u.instrument = o.instrument;
    u.side = o.side;
    u.price = price;
    u.amount = amount;
    u.remaining = remaining;
    u.exch_time = t;
    loop_.notify(u);
}

void ExchangeSim::purge(InstrumentId id, Nanos now) {
    auto& v = orders(id);
    std::erase_if(v, [&](const Resting& r) {
        return r.remaining <= kEps || (r.dead_at != kNever && now - r.dead_at > cfg_.late_window);
    });
    if (v.empty() && static_cast<size_t>(id) < mids_.size()) mids_[static_cast<size_t>(id)] = {};
}

bool ExchangeSim::has_orders(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return id >= 0 && i < orders_.size() && !orders_[i].empty();
}

void ExchangeSim::note(std::deque<MidPoint>& h, Nanos t, double px, Nanos now) {
    // The message that arrived last is the channel's current state, in effect from its own
    // stamp. Stamps can go backwards (also within one channel), so entries stamped at or after
    // it are dropped: they describe a state this message has replaced.
    while (!h.empty() && h.back().time >= t) h.pop_back();
    const bool same = !h.empty() && (h.back().px == px || (std::isnan(h.back().px) && std::isnan(px)));
    if (!same) h.push_back({t, px});
    // Keep one entry at or before the oldest time a late trade is still expected to ask about.
    const Nanos oldest = now - cfg_.late_window;
    while (h.size() >= 2 && h[1].time <= oldest) h.pop_front();
}

void ExchangeSim::note_book(InstrumentId id, const MarketState& exch, Nanos now) {
    const OrderBook* b = exch.book(id);
    if (b == nullptr) return;
    auto i = static_cast<size_t>(id);
    if (i >= mids_.size()) mids_.resize(i + 1);
    note(mids_[i].book, b->last_update(), book_mid(b), now);
}

void ExchangeSim::note_mark(InstrumentId id, const MarketState& exch, Nanos now) {
    const Ticker* k = exch.ticker(id);
    if (k == nullptr) return;
    auto i = static_cast<size_t>(id);
    if (i >= mids_.size()) mids_.resize(i + 1);
    note(mids_[i].mark, instr(id).ticker_time, k->mark_price > 0 ? k->mark_price : kNaN, now);
}

double ExchangeSim::mid_at(InstrumentId id, Nanos t, const MarketState& exch) {
    // The value in effect at t; before the history starts, its earliest value.
    auto at = [t](const std::deque<MidPoint>& h) {
        for (auto it = h.rbegin(); it != h.rend(); ++it) {
            if (it->time <= t) return it->px;
        }
        return h.empty() ? kNaN : h.front().px;
    };
    auto i = static_cast<size_t>(id);
    if (i < mids_.size()) {
        double px = at(mids_[i].book);
        if (!(px > 0)) px = at(mids_[i].mark);
        if (px > 0) {
            const InstrumentDef* d = exch.def(id);
            return d == nullptr || d->kind == InstrumentKind::Option ? px : -1.0 / px;
        }
    }
    return unit_value(id, exch);
}

// ---------------------------------------------------------------------------------------------
// Clock: expiries, settlement, markouts, pnl rows

void ExchangeSim::on_clock(Nanos now, const MarketState& exch) { advance(now, exch); }

void ExchangeSim::advance(Nanos now, const MarketState& exch) {
    if (now <= 0) return;

    while (!expiries_.empty() && expiries_.begin()->first <= now) {
        auto node = expiries_.extract(expiries_.begin());
        for (InstrumentId id : node.mapped()) expire(id, node.key(), exch);
    }
    if (!awaiting_.empty()) {
        const std::vector<InstrumentId> due = awaiting_;
        for (InstrumentId id : due) {
            const Instr& s = instr(id);
            if (now >= s.settle_deadline) {
                settle(id, std::isfinite(s.est_delivery) ? s.est_delivery : exch.index_price(),
                       false, exch, now);
            }
        }
    }
    while (!timers_.empty() && timers_.top().release <= now) {
        const Markout m = timers_.pop().value;
        accounts_[m.account].markout(m.fill, m.horizon, unit_value(m.instrument, exch));
    }

    if (!started_) return;  // instrument lists can be stamped long before the first market data
    if (next_sample_ == 0) {
        for (size_t a = 0; a < accounts_.size(); ++a) write_row(a, exch, now, true);
        next_sample_ = (now / cfg_.pnl_interval + 1) * cfg_.pnl_interval;
    }
    while (next_sample_ <= now) {
        for (size_t a = 0; a < accounts_.size(); ++a) write_row(a, exch, next_sample_, false);
        next_sample_ += cfg_.pnl_interval;
    }
}

void ExchangeSim::finish() {
    const MarketState& exch = loop_.exchange();
    const Nanos now = loop_.now();
    advance(now, exch);
    for (size_t a = 0; a < accounts_.size(); ++a) write_row(a, exch, now, true);
}

void ExchangeSim::write_row(size_t a, const MarketState& exch, Nanos time, bool force) {
    Account& acc = accounts_[a];
    PnlRow row;
    const auto& pos = acc.positions();
    for (size_t i = 0; i < pos.size(); ++i) {
        if (pos[i].qty == 0 && pos[i].young == 0) continue;
        const auto id = static_cast<InstrumentId>(i);
        acc.mark(id, unit_value(id, exch));
        const double qty = pos[i].qty;
        if (std::abs(qty) <= kEps) continue;
        ++row.positions;

        const InstrumentDef* d = exch.def(id);
        if (d == nullptr) continue;
        if (d->kind != InstrumentKind::Option) {
            row.delta += -qty * pos[i].last_u;  // qty USD / price
            continue;
        }
        const Ticker* k = exch.ticker(id);
        const double T = static_cast<double>(d->expiry - time) / static_cast<double>(kYear);
        if (k == nullptr || instr(id).expired || !(T > 0) || !(k->underlying_price > 0) ||
            !(k->mark_iv > 0)) {
            continue;
        }
        const auto g = pricing::black76(k->underlying_price, d->strike, T, k->mark_iv / 100.0, d->is_call);
        row.delta += qty * pricing::inverse_delta(g.delta, pricing::to_btc(g.price, k->underlying_price));
        row.gamma += qty * g.gamma;
        row.vega += qty * g.vega / 100.0;
    }

    RowState& rs = rows_[a];
    const bool flat = row.positions == 0;
    const bool same = rs.written && rs.flat && flat && rs.cash == acc.cash();
    if (!force && same) return;
    rs = {true, flat, acc.cash()};
    if (sink_ == nullptr) return;

    row.time = time;
    row.equity = acc.equity();
    row.cash = acc.cash();
    row.pnl = acc.pnl();
    row.index = exch.index_price();
    sink_->pnl_row(a, row);
}

// ---------------------------------------------------------------------------------------------
// Expiry

void ExchangeSim::expire(InstrumentId id, Nanos expiry, const MarketState& exch) {
    Instr& s = instr(id);
    if (s.expired) return;
    OrderRequest req;
    req.instrument = id;
    for (auto& r : orders(id)) {
        if (r.dead_at != kNever || r.remaining <= kEps) continue;
        r.dead_at = expiry;
        req.account = r.account;
        req.side = r.side;
        send(req, r.id, UpdateKind::Cancelled, Reject::Expired, r.price, 0, 0, expiry);
    }

    // Mark at the settlement value from the best estimate so far. The final delivery price
    // arrives on a ticker a few seconds later (settle()).
    const InstrumentDef* d = exch.def(id);
    const double S = std::isfinite(s.est_delivery) ? s.est_delivery : exch.index_price();
    const double u = d != nullptr ? intrinsic_btc(*d, S) : kNaN;
    s.frozen_u = std::isfinite(u) ? u : unit_value(id, exch);
    s.expired = true;
    s.awaiting = true;
    s.settle_deadline = expiry + cfg_.settle_wait;
    awaiting_.push_back(id);
    for (auto& acc : accounts_) {
        if (acc.position(id) != 0) acc.mark(id, s.frozen_u);
    }
}

void ExchangeSim::settle(InstrumentId id, double delivery_price, bool final_price,
                         const MarketState& exch, Nanos now) {
    Instr& s = instr(id);
    if (!s.awaiting) return;
    s.awaiting = false;
    std::erase(awaiting_, id);

    const InstrumentDef* d = exch.def(id);
    if (d == nullptr) return;
    const double u = intrinsic_btc(*d, delivery_price);
    if (std::isfinite(u)) s.frozen_u = u;

    bool any = false;
    OrderRequest req;
    req.instrument = id;
    for (size_t a = 0; a < accounts_.size(); ++a) {
        Account& acc = accounts_[a];
        const auto& pos = acc.positions();
        const auto i = static_cast<size_t>(id);
        if (i >= pos.size() || (pos[i].qty == 0 && pos[i].young == 0)) continue;
        const double qty = pos[i].qty;
        double fee = 0;
        if (std::abs(qty) > kEps) {
            try {
                fee = fees_.delivery_fee(d->settlement_period, s.frozen_u, qty);
            } catch (const std::out_of_range&) {
                throw std::runtime_error("no delivery fee for settlement period '" +
                                         d->settlement_period + "' (" + d->name + ")");
            }
            ++stats_[a].settlements;
            stats_[a].delivery_fees += fee;
            any = true;
        }
        acc.settle(id, s.frozen_u, fee);
        req.account = static_cast<std::uint32_t>(a);
        send(req, 0, UpdateKind::Settled, Reject::None, 0, 0, 0, now);
        write_row(a, exch, now, true);
    }
    if (any) ++(final_price ? settled_final_ : settled_estimate_);
}

// ---------------------------------------------------------------------------------------------
// Market events

void ExchangeSim::on_exchange(const Event& e, ApplyResult r, const MarketState& exch, Nanos now) {
    if (const auto* u = std::get_if<BookUpdate>(&e.payload)) {
        started_ = true;
        on_book(*u, r, e.time, exch);
        if (has_orders(u->id)) note_book(u->id, exch, now);
    } else if (const auto* k = std::get_if<Ticker>(&e.payload)) {
        Instr& s = instr(k->id);
        s.ticker_time = e.time;
        // The ticker's mark price is the mark while the book is one-sided or invalid.
        if (has_orders(k->id)) note_mark(k->id, exch, now);
        if (!std::isfinite(k->est_delivery_price) && !std::isfinite(k->delivery_price)) return;
        if (std::isfinite(k->est_delivery_price)) s.est_delivery = k->est_delivery_price;
        if (std::isfinite(k->delivery_price) && s.awaiting) settle(k->id, k->delivery_price, true, exch, now);
    } else if (const auto* tr = std::get_if<Trade>(&e.payload)) {
        on_trade(*tr, e.time, exch, now);
    } else if (const auto* rec = std::get_if<RecorderEvent>(&e.payload)) {
        // A connect / disconnect invalidates every book at once, with no book message.
        if (rec->kind != RecorderEvent::Kind::Connect && rec->kind != RecorderEvent::Kind::Disconnect) return;
        for (auto& h : mids_) {
            if (!h.book.empty()) note(h.book, now, kNaN, now);
        }
    } else if (const auto* d = std::get_if<InstrumentDef>(&e.payload)) {
        if (d->kind != InstrumentKind::Option) return;
        Instr& s = instr(d->id);
        if (!s.registered) {
            s.registered = true;
            expiries_[d->expiry].push_back(d->id);
        }
    }
}

void ExchangeSim::on_book(const BookUpdate& u, ApplyResult r, Nanos t, const MarketState& exch) {
    auto i = static_cast<size_t>(u.id);
    if (i >= orders_.size() || orders_[i].empty()) return;
    auto& v = orders_[i];
    if (r == ApplyResult::Snapshot) {
        // The chain was broken (or this is the first snapshot): what happened at our level in
        // between is unknown, so every order goes to the back of what is displayed now.
        const OrderBook* b = exch.book(u.id);
        for (auto& o : v) model_->on_join(o, level_size(*b, o.side, o.price));
    } else if (r == ApplyResult::Ok) {
        for (auto& o : v) {
            for (const auto& entry : o.side == Side::Buy ? u.bids : u.asks) {
                if (entry.price != o.price) continue;
                model_->on_level(o, entry.action == BookAction::Delete ? 0.0 : entry.amount, t);
            }
        }
    }
}

void ExchangeSim::on_trade(const Trade& tr, Nanos t, const MarketState& exch, Nanos now) {
    auto i = static_cast<size_t>(tr.id);
    if (i >= orders_.size() || orders_[i].empty()) return;
    auto& v = orders_[i];

    // A buy-aggressor trade lifts asks (our sells); a sell-aggressor trade hits bids (our buys).
    const Side side = tr.buy ? Side::Sell : Side::Buy;
    std::vector<Resting*> hit;
    for (auto& o : v) {
        if (o.side != side || o.remaining <= kEps || !o.live_at(t)) continue;
        const bool reached = side == Side::Buy ? tr.price <= o.price : tr.price >= o.price;
        if (reached) hit.push_back(&o);
    }
    // Per account, best price first: the aggressor would have met that order first.
    std::sort(hit.begin(), hit.end(), [side](const Resting* a, const Resting* b) {
        if (a->account != b->account) return a->account < b->account;
        if (a->price != b->price) return side == Side::Buy ? a->price > b->price : a->price < b->price;
        if (a->active_from != b->active_from) return a->active_from < b->active_from;
        return a->id < b->id;
    });

    // Fills come in whole lots (0.1 contract for BTC options), rounded down: the queue-aware
    // model's pro-rata cancels leave fractional queue sizes, the exchange does not trade them.
    const InstrumentDef* d = exch.def(tr.id);
    const double lot = d != nullptr && d->min_trade_amount > 0 ? d->min_trade_amount : 0.0;

    double avail = 0;
    const Resting* prev = nullptr;
    for (Resting* o : hit) {
        if (prev == nullptr || prev->account != o->account) avail = tr.amount;  // accounts don't compete
        prev = o;
        double fill = model_->on_trade(*o, tr.price != o->price, tr.amount, avail, t);
        if (lot > 0) fill = std::floor(fill / lot + kEps) * lot;
        if (fill <= kEps) continue;
        o->remaining = std::max(0.0, o->remaining - fill);
        if (o->remaining <= kEps) o->remaining = 0;
        avail = std::max(0.0, avail - fill);
        apply_fill(o->account, o->id, tr.id, side, o->price, fill, o->remaining, true, t, exch, now);
    }
    purge(tr.id, now);
}

void ExchangeSim::apply_fill(std::uint32_t account, std::uint64_t client_id, InstrumentId id,
                             Side side, Px price, double amount, double remaining, bool maker,
                             Nanos t, const MarketState& exch, Nanos now) {
    const InstrumentDef* d = exch.def(id);
    const bool option = d == nullptr || d->kind == InstrumentKind::Option;
    const double px = from_e8(price);

    FillRecord f;
    f.time = t;
    f.instrument = id;
    f.client_id = client_id;
    f.side = side;
    f.option = option;
    f.maker = maker;
    f.price = px;
    f.amount = amount;
    f.fee = option ? fees_.option_fee(px, amount, maker) : fees_.perp_fee(px, amount, maker);
    f.u_fill = option ? px : -1.0 / px;
    // A maker fill happened at the trade's time t, which can be well behind the clock.
    f.u_mid = maker ? mid_at(id, t, exch) : unit_value(id, exch);
    if (std::isfinite(f.u_mid)) f.mid = option ? f.u_mid : -1.0 / f.u_mid;

    const size_t idx = accounts_[account].fill(f);
    for (size_t h = 0; h < kMarkoutHorizons.size(); ++h) {
        timers_.push(t + kMarkoutHorizons[h], timer_seq_++, {account, idx, h, id});
    }
    ++(maker ? stats_[account].maker_fills : stats_[account].taker_fills);

    OrderRequest req;
    req.account = account;
    req.instrument = id;
    req.side = side;
    send(req, client_id, UpdateKind::Fill, Reject::None, price, amount, remaining, t);
    write_row(account, exch, now, true);
}

// ---------------------------------------------------------------------------------------------
// Our orders

void ExchangeSim::on_order(const OrderRequest& o, const MarketState& exch, Nanos now) {
    advance(now, exch);
    if (o.account >= accounts_.size()) return;  // nobody to answer
    if (o.instrument < 0) {
        send(o, o.client_id, UpdateKind::Rejected, Reject::BadRequest, 0, 0, 0, now);
        return;
    }
    purge(o.instrument, now);
    switch (o.action) {
        case OrderAction::Place:
            if (o.type == OrderType::Ioc) take(o, exch, now);
            else place(o, exch, now);
            break;
        case OrderAction::Cancel: cancel(o, now); break;
        case OrderAction::Replace: replace(o, exch, now); break;
    }
}

Reject ExchangeSim::check_post_only(InstrumentId id, Side side, Px price, double amount,
                                    const MarketState& exch, Nanos now) {
    const InstrumentDef* d = exch.def(id);
    if (d == nullptr || !(amount > 0) || price <= 0) return Reject::BadRequest;
    if (d->kind == InstrumentKind::Option && (instr(id).expired || now >= d->expiry)) return Reject::Expired;
    const OrderBook* b = exch.book(id);
    if (b == nullptr || !b->valid()) return Reject::NoBook;
    if (side == Side::Buy) {
        if (auto a = b->best_ask(); a && price >= a->price) return Reject::WouldCross;
    } else {
        if (auto bb = b->best_bid(); bb && price <= bb->price) return Reject::WouldCross;
    }
    return Reject::None;
}

void ExchangeSim::place(const OrderRequest& o, const MarketState& exch, Nanos now) {
    const Reject why = check_post_only(o.instrument, o.side, o.price, o.amount, exch, now);
    if (why != Reject::None) {
        send(o, o.client_id, UpdateKind::Rejected, why, o.price, 0, 0, now);
        return;
    }
    Resting r;
    r.account = o.account;
    r.id = o.client_id;
    r.side = o.side;
    r.price = o.price;
    r.remaining = o.amount;
    r.active_from = now;
    model_->on_join(r, level_size(*exch.book(o.instrument), o.side, o.price));
    orders(o.instrument).push_back(r);
    note_book(o.instrument, exch, now);
    note_mark(o.instrument, exch, now);
    ++stats_[o.account].placed;
    send(o, o.client_id, UpdateKind::Accepted, Reject::None, o.price, 0, o.amount, now);
}

void ExchangeSim::cancel(const OrderRequest& o, Nanos now) {
    for (auto& r : orders(o.instrument)) {
        if (r.account != o.account || r.id != o.client_id || r.dead_at != kNever) continue;
        r.dead_at = now;
        OrderRequest req = o;
        req.side = r.side;
        send(req, r.id, UpdateKind::Cancelled, Reject::None, r.price, 0, 0, now);
        return;
    }
    // Already filled or gone.
    send(o, o.client_id, UpdateKind::Rejected, Reject::UnknownOrder, 0, 0, 0, now);
}

void ExchangeSim::replace(const OrderRequest& o, const MarketState& exch, Nanos now) {
    auto& v = orders(o.instrument);
    auto it = std::find_if(v.begin(), v.end(), [&](const Resting& r) {
        return r.account == o.account && r.id == o.client_id && r.dead_at == kNever;
    });
    OrderRequest req = o;
    if (it == v.end()) {
        send(req, o.new_id, UpdateKind::Rejected, Reject::UnknownOrder, o.price, 0, 0, now);
        return;
    }
    // The old order is gone either way, like a cancel followed by a new order.
    it->dead_at = now;
    const Resting old = *it;
    req.side = old.side;
    const Reject why = check_post_only(o.instrument, old.side, o.price, o.amount, exch, now);
    if (why != Reject::None) {
        send(req, o.new_id, UpdateKind::Rejected, why, o.price, 0, 0, now);
        return;
    }
    Resting r = old;
    r.id = o.new_id;
    r.remaining = o.amount;
    r.active_from = now;
    r.dead_at = kNever;
    // Same price and no larger: keeps its place in the queue. Anything else joins the back.
    if (o.price != old.price || o.amount > old.remaining + kEps) {
        r.price = o.price;
        model_->on_join(r, level_size(*exch.book(o.instrument), r.side, r.price));
    }
    v.push_back(r);
    note_book(o.instrument, exch, now);
    note_mark(o.instrument, exch, now);
    ++stats_[o.account].replaced;
    send(req, o.new_id, UpdateKind::Accepted, Reject::None, o.price, 0, o.amount, now);
}

void ExchangeSim::take(const OrderRequest& o, const MarketState& exch, Nanos now) {
    const InstrumentDef* d = exch.def(o.instrument);
    const OrderBook* b = exch.book(o.instrument);
    Reject why = Reject::None;
    if (d == nullptr || !(o.amount > 0) || o.price <= 0) why = Reject::BadRequest;
    else if (d->kind == InstrumentKind::Option && (instr(o.instrument).expired || now >= d->expiry)) why = Reject::Expired;
    else if (b == nullptr || !b->valid()) why = Reject::NoBook;
    if (why != Reject::None) {
        send(o, o.client_id, UpdateKind::Rejected, why, o.price, 0, 0, now);
        return;
    }
    // Walk the recorded book from the best price. The book itself is not changed: our taker
    // orders do not use up liquidity for later orders (a stated limit).
    double rem = o.amount;
    auto walk = [&](const auto& levels, auto worse) {
        for (const auto& [px, amt] : levels) {
            if (rem <= kEps || worse(px)) break;
            const double fill = std::min(amt, rem);
            rem -= fill;
            if (rem <= kEps) rem = 0;
            apply_fill(o.account, o.client_id, o.instrument, o.side, px, fill, rem, false, now, exch, now);
        }
    };
    if (o.side == Side::Buy) walk(b->asks(), [&](Px px) { return px > o.price; });
    else walk(b->bids(), [&](Px px) { return px < o.price; });
    if (rem > 0) send(o, o.client_id, UpdateKind::Cancelled, Reject::None, o.price, 0, rem, now);
}

}  // namespace mm
