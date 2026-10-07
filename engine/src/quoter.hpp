// The part every quoting strategy shares. One Quoter per account: it asks the strategy for a
// bid and ask per option, applies the rules below, and keeps at most one resting order per
// option and side in line with the answer (place / replace / cancel).
//
// Rules, all on the strategy view (market data after latency):
//   which options   |delta| from the ticker inside [min_abs_delta, max_abs_delta]
//   pulled when     the book is invalid (gap, disconnect), neither book nor ticker updated for
//                   stale_after, or expiry is closer than no_quote_before_expiry
//   prices          bid rounded down, ask rounded up to the tick; a quote that would cross the
//                   visible book is moved one tick inside it (orders are post-only)
//   risk limits     no bid at +max_position in that option, no ask at -max_position;
//                   no bids at all at +max_vega for the account, no asks at -max_vega
//   pace            an option is re-quoted at most once per requote_interval
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "event_loop.hpp"
#include "strategy.hpp"

namespace mm {

struct QuoterConfig {
    double size = 0.1;          // contracts per quote
    double max_position = 1.0;  // contracts per option, either direction
    double max_vega = 100.0;    // USD per vol point for the account, either direction
    double min_abs_delta = 0.10, max_abs_delta = 0.90;
    Nanos stale_after = 10 * kSec;
    Nanos no_quote_before_expiry = 3600 * kSec;
    Nanos requote_interval = 100 * kMs;
    Nanos fit_max_age = 30 * kSec;  // a SABR fit older than this is not used
};

struct QuoterStats {
    std::int64_t evaluations = 0, places = 0, replaces = 0, cancels = 0;
    std::vector<InstrumentId> quoted;  // every option an order was sent for, in first-use order
};

class Quoter final : public MarketObserver, public OrderObserver {
public:
    Quoter(EventLoop& loop, std::uint32_t account, std::unique_ptr<Strategy> strategy,
           QuoterConfig cfg);

    void on_market(const Event& e, const MarketState& view, Nanos now) override;
    void on_order_update(const OrderUpdate& u, const MarketState& view, Nanos now) override;

    const Strategy& strategy() const { return *strategy_; }
    const QuoterConfig& config() const { return cfg_; }
    const QuoterStats& stats() const { return stats_; }
    // As known to the strategy (fills reported so far).
    double position(InstrumentId id) const;
    double account_vega() const { return vega_; }

private:
    enum class SlotState : std::uint8_t { None, Pending, Live, Cancelling };
    struct Slot {
        SlotState state = SlotState::None;
        std::uint64_t id = 0;
        Px price = 0;
        double remaining = 0;
    };
    struct State {
        Slot bid, ask;
        double position = 0;
        Nanos last_eval = 0;
        Nanos last_ticker = 0;  // exchange time of the latest ticker
        bool quoted = false;
    };

    State& state(InstrumentId id);
    void maybe_evaluate(InstrumentId id, const MarketState& view, Nanos now);
    void evaluate(InstrumentId id, State& st, const MarketState& view, Nanos now);
    // The quote we want now; an empty side = nothing resting on that side.
    void desired(InstrumentId id, const State& st, const MarketState& view, Nanos now,
                 std::optional<Px>& bid, std::optional<Px>& ask);
    void reconcile(InstrumentId id, Side side, State& st, Slot& slot, std::optional<Px> want);
    void sweep(const MarketState& view, Nanos now);
    void update_vega(const MarketState& view, Nanos now);

    EventLoop& loop_;
    std::uint32_t account_;
    std::unique_ptr<Strategy> strategy_;
    QuoterConfig cfg_;
    QuoterStats stats_;
    std::vector<State> states_;  // by instrument
    std::uint64_t next_id_ = 1;
    Nanos next_sweep_ = 0;
    double vega_ = 0;
};

}  // namespace mm
