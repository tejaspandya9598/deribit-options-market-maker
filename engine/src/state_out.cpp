#include "state_out.hpp"

#include <cmath>
#include <map>

#include <nlohmann/json.hpp>

namespace mm {

namespace {

using json = nlohmann::ordered_json;

json num(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }

}  // namespace

StateWriter::StateWriter(std::FILE* out, const ExchangeSim& sim, const Instruments& reg, Nanos interval)
    : out_(out), sim_(sim), reg_(reg), interval_(interval), fills_seen_(sim.accounts(), 0) {}

void StateWriter::on_exchange(const Event& e, ApplyResult, const MarketState&, Nanos) {
    if (std::holds_alternative<BookUpdate>(e.payload)) started_ = true;
    else if (const auto* d = std::get_if<InstrumentDef>(&e.payload)) {
        if (d->kind == InstrumentKind::Perpetual) perp_ = d->id;
    }
}

void StateWriter::on_clock(Nanos now, const MarketState& exch) {
    if (!started_ || now <= 0) return;
    if (next_ == 0) next_ = (now / interval_ + 1) * interval_;
    if (now < next_) return;
    // One state per call: after a gap in the data the skipped intervals would all be the same.
    emit(exch, next_);
    next_ = (now / interval_ + 1) * interval_;
}

void StateWriter::finish(const MarketState& exch, Nanos now) {
    emit(exch, now);
    std::fputs("{\"type\":\"end\"}\n", out_);
    std::fflush(out_);
}

void StateWriter::emit(const MarketState& exch, Nanos t) {
    const size_t n = sim_.accounts();

    // Our live orders, per account and instrument.
    struct TwoSided {
        Px bid = 0, ask = 0;
        double bid_amt = 0, ask_amt = 0;
    };
    std::vector<std::map<InstrumentId, TwoSided>> quotes(n);
    std::map<InstrumentId, bool> shown;
    for (size_t i = 0; i < sim_.order_slots(); ++i) {
        const auto id = static_cast<InstrumentId>(i);
        for (const Resting& r : sim_.resting(id)) {
            if (r.dead_at != kNever || r.remaining <= 0 || r.account >= n) continue;
            TwoSided& q = quotes[r.account][id];
            if (r.side == Side::Buy) {
                q.bid = r.price;
                q.bid_amt = r.remaining;
            } else {
                q.ask = r.price;
                q.ask_amt = r.remaining;
            }
            shown[id] = true;
        }
    }

    json accounts = json::array();
    json fills = json::array();
    for (size_t a = 0; a < n; ++a) {
        const PnlRow row = sim_.peek(a, exch, t);  // read-only: a watched run stays the same run
        const Account& acc = sim_.account(a);

        json positions = json::array();
        const auto& pos = acc.positions();
        for (size_t i = 0; i < pos.size(); ++i) {
            if (std::abs(pos[i].qty) <= 1e-9) continue;
            const auto id = static_cast<InstrumentId>(i);
            const InstrumentDef* d = exch.def(id);
            const bool option = d == nullptr || d->kind == InstrumentKind::Option;
            double u = sim_.peek_value(id, exch);
            if (!std::isfinite(u)) u = pos[i].last_u;
            const double mark = option ? u : -1.0 / u;
            positions.push_back({reg_.name(id), pos[i].qty, num(mark)});
            shown[id] = true;
        }
        json q = json::array();
        for (const auto& [id, two] : quotes[a]) {
            q.push_back({reg_.name(id), two.bid ? json(from_e8(two.bid)) : json(nullptr), two.bid_amt,
                         two.ask ? json(from_e8(two.ask)) : json(nullptr), two.ask_amt});
        }
        const auto& all = acc.fills();
        for (size_t k = fills_seen_[a]; k < all.size(); ++k) {
            const FillRecord& f = all[k];
            fills.push_back({{"a", a},
                             {"t", f.time},
                             {"i", reg_.name(f.instrument)},
                             {"side", f.side == Side::Buy ? "buy" : "sell"},
                             {"px", f.price},
                             {"amt", f.amount},
                             {"maker", f.maker},
                             {"fee", f.fee}});
        }
        fills_seen_[a] = all.size();

        accounts.push_back({{"name", acc.name()},
                            {"equity", row.equity},
                            {"pnl",
                             {{"total", row.pnl.total()},
                              {"spread", row.pnl.spread},
                              {"fees", row.pnl.fees},
                              {"adverse", row.pnl.adverse},
                              {"option_mtm", row.pnl.option_mtm},
                              {"hedge", row.pnl.hedge}}},
                            {"delta", row.delta},
                            {"gamma", row.gamma},
                            {"vega", row.vega},
                            {"fills", all.size()},
                            {"positions", std::move(positions)},
                            {"quotes", std::move(q)}});
    }

    if (perp_ >= 0) shown[perp_] = true;
    json books = json::array();
    for (const auto& [id, yes] : shown) {
        const OrderBook* b = exch.book(id);
        const Ticker* k = exch.ticker(id);
        const auto bid = b != nullptr ? b->best_bid() : std::nullopt;
        const auto ask = b != nullptr ? b->best_ask() : std::nullopt;
        books.push_back({reg_.name(id), bid ? json(from_e8(bid->price)) : json(nullptr),
                         ask ? json(from_e8(ask->price)) : json(nullptr),
                         k != nullptr ? num(k->mark_iv) : json(nullptr)});
    }

    const json state = {{"type", "state"},
                        {"t", t},
                        {"index", num(exch.index_price())},
                        {"accounts", std::move(accounts)},
                        {"fills", std::move(fills)},
                        {"books", std::move(books)}};
    const std::string line = state.dump();
    std::fwrite(line.data(), 1, line.size(), out_);
    std::fputc('\n', out_);
    std::fflush(out_);
}

}  // namespace mm
