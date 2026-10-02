#include "order_book.hpp"

namespace mm {

namespace {

template <class Side>
void apply_entries(Side& side, const std::vector<BookEntry>& entries) {
    for (const auto& e : entries) {
        if (e.action == BookAction::Delete) {
            side.erase(e.price);
        } else {
            side[e.price] = e.amount;
        }
    }
}

}  // namespace

ApplyResult OrderBook::apply(const BookUpdate& u, Nanos t) {
    ApplyResult r;
    if (u.snapshot) {
        bids_.clear();
        asks_.clear();
        apply_entries(bids_, u.bids);
        apply_entries(asks_, u.asks);
        valid_ = true;
        r = ApplyResult::Snapshot;
    } else if (valid_ && u.prev_change_id == change_id_) {
        apply_entries(bids_, u.bids);
        apply_entries(asks_, u.asks);
        r = ApplyResult::Ok;
    } else {
        r = seen_ && valid_ ? ApplyResult::Gap : ApplyResult::NoSnapshot;
        invalidate();
    }
    seen_ = true;
    change_id_ = u.change_id;
    last_update_ = t;
    return r;
}

void OrderBook::invalidate() {
    bids_.clear();
    asks_.clear();
    valid_ = false;
}

std::optional<Level> OrderBook::best_bid() const {
    if (!valid_ || bids_.empty()) return std::nullopt;
    return Level{bids_.begin()->first, bids_.begin()->second};
}

std::optional<Level> OrderBook::best_ask() const {
    if (!valid_ || asks_.empty()) return std::nullopt;
    return Level{asks_.begin()->first, asks_.begin()->second};
}

}  // namespace mm
