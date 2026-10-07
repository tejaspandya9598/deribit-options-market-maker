// Fill models: when does a recorded trade fill one of our resting (maker) orders?
//
// A sell-aggressor trade can fill our bids, a buy-aggressor trade our asks.
//   at       the trade printed at our price
//   through  the trade printed past our price (below our bid / above our ask)
//
// Optimistic   any trade at or through our price fills us, up to the trade's size.
// QueueAware   our order joins the back of the queue: everything displayed at its price when it
//              arrives is ahead of it.
//                - a trade at our price eats the queue ahead first; only what is left fills us
//                - a size drop at our level that no trade explains is a cancel; it shrinks the
//                  queue ahead in proportion (queue_ahead / level size)
//                - a trade through our price means nothing was ahead any more: fill
//
// trade_through decides the size of a fill from a trade through our price, in both models:
//   TradeSize  at most the trade's size (default). The aggressor only sold/bought that much, so
//              that is all our better-priced order could have taken from it.
//   Full       the whole order, as if a print through our price always swept our level.
// With either setting the optimistic model never fills less than the queue-aware one on the
// same order and trade.
//
// The trade and book channels are batched separately, so a trade print and the book change it
// caused can arrive in either order. The queue-aware model reconciles the two (see on_trade),
// so one trade is never counted against the queue twice.
#pragma once

#include <cstdint>
#include <limits>
#include <memory>

#include "orders.hpp"

namespace mm {

inline constexpr Nanos kNever = std::numeric_limits<Nanos>::max();

enum class FillModelKind : std::uint8_t { Optimistic, QueueAware };
enum class TradeThrough : std::uint8_t { TradeSize, Full };

// One of our maker orders on the exchange side.
struct Resting {
    std::uint32_t account = 0;
    std::uint64_t id = 0;
    Side side = Side::Buy;
    Px price = 0;
    double remaining = 0;
    Nanos active_from = 0;    // exchange time it arrived; only later trades can fill it
    Nanos dead_at = kNever;   // exchange time it was cancelled / replaced / fully filled

    // Queue state (used by QueueAware; kept for both).
    double queue_ahead = 0;   // displayed size ahead of us at our price
    double level_size = 0;    // displayed size at our price, as of the last book message
    double trade_credit = 0;  // trades at our price already taken off the queue, whose book
                              // change has not arrived yet
    Nanos credit_time = 0;
    double guess_cancel = 0;  // level drops taken as cancels, in case their trade print is late
    double guess_cut = 0;     // what those drops removed from queue_ahead
    Nanos guess_time = 0;

    bool live_at(Nanos t) const { return active_from <= t && t < dead_at; }
};

class FillModel {
public:
    virtual ~FillModel() = default;

    // The order starts (or restarts, after a book snapshot) at the back of a level of this size.
    void on_join(Resting& o, double level_size) const;
    // The displayed size at the order's price changed (book message at exchange time t).
    void on_level(Resting& o, double new_size, Nanos t) const;
    // A trade on the order's side at or through its price. trade_amount = the print's size;
    // avail = what is left of it for this account after its better-priced orders.
    // Returns the amount filled (the caller reduces `remaining` and `avail`).
    virtual double on_trade(Resting& o, bool through, double trade_amount, double avail,
                            Nanos t) const = 0;

protected:
    FillModel(TradeThrough tt, Nanos late_window) : through_(tt), late_window_(late_window) {}
    double through_fill(const Resting& o, double avail) const;

    TradeThrough through_;
    // How long a trade print and its book change may be apart and still be matched up.
    Nanos late_window_;
};

std::unique_ptr<FillModel> make_fill_model(FillModelKind kind, TradeThrough tt, Nanos late_window);

const char* to_string(FillModelKind k);
const char* to_string(TradeThrough t);

}  // namespace mm
