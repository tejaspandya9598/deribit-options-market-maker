#include "market_state.hpp"

namespace mm {

template <class T>
T& MarketState::slot(std::vector<T>& v, InstrumentId id) {
    auto i = static_cast<size_t>(id);
    if (i >= v.size()) v.resize(i + 1);
    return v[i];
}

ApplyResult MarketState::apply(const Event& e) {
    struct Visitor {
        MarketState& s;
        Nanos t;
        ApplyResult operator()(const BookUpdate& u) { return slot(s.books_, u.id).apply(u, t); }
        ApplyResult operator()(const Trade&) { return ApplyResult::NotBook; }
        ApplyResult operator()(const Ticker& k) {
            slot(s.tickers_, k.id) = k;
            return ApplyResult::NotBook;
        }
        ApplyResult operator()(const IndexPrice& x) {
            s.index_ = x.price;
            return ApplyResult::NotBook;
        }
        ApplyResult operator()(const SabrFit& f) {
            s.fits_[f.expiry] = f;
            return ApplyResult::NotBook;
        }
        ApplyResult operator()(const InstrumentDef& d) {
            slot(s.defs_, d.id) = d;
            return ApplyResult::NotBook;
        }
        ApplyResult operator()(const RecorderEvent& r) {
            // After a disconnect, or a new connection (a previous session may have stopped
            // without logging one), nothing is known until each book's next snapshot.
            if (r.kind == RecorderEvent::Kind::Disconnect || r.kind == RecorderEvent::Kind::Connect) {
                for (auto& b : s.books_) b.invalidate();
            }
            return ApplyResult::NotBook;
        }
    };
    return std::visit(Visitor{*this, e.time}, e.payload);
}

const OrderBook* MarketState::book(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return i < books_.size() ? &books_[i] : nullptr;
}

const Ticker* MarketState::ticker(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return i < tickers_.size() && tickers_[i] ? &*tickers_[i] : nullptr;
}

const InstrumentDef* MarketState::def(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return i < defs_.size() && defs_[i] ? &*defs_[i] : nullptr;
}

}  // namespace mm
