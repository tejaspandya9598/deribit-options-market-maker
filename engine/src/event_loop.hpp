// Event loop with an exchange-time clock and a latency queue in each direction.
//
//   input event at exchange time T
//     1. release every queued item due at or before T, in (release, seq) order; the clock is set
//        to each item's release time as it is delivered
//     2. clock = max(clock, T); apply the event to the exchange-side state; notify exchange
//        observers (true state: fills in M4, the top-of-book dump)
//     3. queue the event for the strategy view at T + md_latency (SABR fits: at T, their time
//        already includes compute time)
//   strategies submit orders -> they reach the exchange side at now + order_latency
//
// Market observers (strategies) only ever get the view state, which is built from delivered
// events. They cannot see anything before event time + latency.
#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "delay_queue.hpp"
#include "events.hpp"
#include "market_state.hpp"

namespace mm {

// Placeholder for M4 (place / cancel / replace). Carried through the order-latency queue.
struct OrderRequest {
    std::uint32_t account = 0;
    std::uint64_t client_id = 0;
};

class MarketObserver {
public:
    virtual ~MarketObserver() = default;
    // `view` already includes `e`. `now` = e.time + md_latency (the release time).
    virtual void on_market(const Event& e, const MarketState& view, Nanos now) = 0;
};

class ExchangeObserver {
public:
    virtual ~ExchangeObserver() = default;
    virtual void on_exchange(const Event& e, ApplyResult r, const MarketState& exch, Nanos now) = 0;
    virtual void on_order(const OrderRequest&, const MarketState&, Nanos) {}
};

// Upper bound for either latency. The sweep tops out at 250 ms; anything near int64 range would
// overflow time + latency.
inline constexpr Nanos kMaxLatency = 60 * kSec;

struct LoopConfig {
    Nanos md_latency = 0;     // exchange -> strategy
    Nanos order_latency = 0;  // strategy -> exchange
};

struct LoopStats {
    std::int64_t events = 0, books = 0, snapshots = 0, gaps = 0, no_snapshot = 0;
    std::int64_t trades = 0, tickers = 0, index = 0, fits = 0, defs = 0, recorder = 0;
    std::int64_t late_events = 0;  // exchange time behind the clock (cross-channel reordering)
};

class EventLoop {
public:
    // Latencies must be in [0, kMaxLatency] (a negative one would be look-ahead).
    explicit EventLoop(LoopConfig cfg);

    void add_market_observer(MarketObserver* o) { market_obs_.push_back(o); }
    void add_exchange_observer(ExchangeObserver* o) { exch_obs_.push_back(o); }

    void process(Event e);
    // End of input: deliver everything still queued.
    void finish();
    // Called by strategies (from on_market): arrives at the exchange side at now + order_latency.
    void submit_order(const OrderRequest& o);

    Nanos now() const { return now_; }
    const LoopConfig& config() const { return cfg_; }
    const LoopStats& stats() const { return stats_; }
    // Exchange-side state, for exchange observers and tests. Strategies must use the view.
    const MarketState& exchange() const { return exch_; }
    const MarketState& view() const { return view_; }

private:
    void release_until(Nanos t);
    void count(const Event& e, ApplyResult r);

    LoopConfig cfg_;
    Nanos now_ = 0;
    std::uint64_t seq_ = 0;
    MarketState exch_, view_;
    DelayQueue<Event> md_q_;
    DelayQueue<OrderRequest> order_q_;
    std::vector<MarketObserver*> market_obs_;
    std::vector<ExchangeObserver*> exch_obs_;
    LoopStats stats_;
};

}  // namespace mm
