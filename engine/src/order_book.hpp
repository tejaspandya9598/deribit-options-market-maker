// One instrument's book, rebuilt from a snapshot plus chained changes.
//
// Rules (check_book.py `tob` follows the same ones, so the two can be compared line by line):
//   snapshot                                   -> reset, valid
//   change, valid, prev_change_id == last id   -> apply
//   anything else                              -> invalid (cleared) until the next snapshot
//   invalidate() (recorder connect/disconnect) -> invalid until the next snapshot
// The last change_id is updated on every message, valid or not.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

#include "events.hpp"

namespace mm {

enum class ApplyResult : std::uint8_t { Ok, Snapshot, Gap, NoSnapshot, NotBook };

struct Level {
    Px price;
    double amount;
};

class OrderBook {
public:
    using Bids = std::map<Px, double, std::greater<Px>>;
    using Asks = std::map<Px, double, std::less<Px>>;

    ApplyResult apply(const BookUpdate& u, Nanos t);
    void invalidate();

    bool valid() const { return valid_; }
    // Not safe to quote on: invalid, or no update for longer than max_age.
    bool stale(Nanos now, Nanos max_age) const { return !valid_ || now - last_update_ > max_age; }

    std::optional<Level> best_bid() const;
    std::optional<Level> best_ask() const;
    const Bids& bids() const { return bids_; }
    const Asks& asks() const { return asks_; }
    std::int64_t change_id() const { return change_id_; }
    Nanos last_update() const { return last_update_; }

private:
    Bids bids_;
    Asks asks_;
    bool valid_ = false;
    bool seen_ = false;
    std::int64_t change_id_ = 0;
    Nanos last_update_ = 0;
};

}  // namespace mm
