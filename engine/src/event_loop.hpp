// Event loop with an exchange-time clock and a latency queue in each direction.
//
//   input event at exchange time T
//     1. release every queued item due at or before T, in (release, seq) order; the clock is set
//        to each item's release time as it is delivered
//     2. clock = max(clock, T); exchange observers get on_clock (timers, with the state as it
//        was before this event); apply the event to the exchange-side state; notify exchange
//        observers (true state: fills, the top-of-book dump)
//     3. queue the event for the strategy view (see MdTiming for when it gets there)
//   strategies submit orders -> they reach the exchange side at now + order_latency
//   the exchange side reports back (acks, fills) -> strategies get it at its time + md_latency
//
// Market observers (strategies) only ever get the view state, which is built from delivered
// events. They cannot see anything before event time + latency, in either timing mode.
#pragma once

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "delay_queue.hpp"
#include "events.hpp"
#include "market_state.hpp"
#include "orders.hpp"

namespace mm {

class MarketObserver {
public:
    virtual ~MarketObserver() = default;
    // `view` already includes `e`. `now` = e.time + md_latency (the release time).
    virtual void on_market(const Event& e, const MarketState& view, Nanos now) = 0;
};

class ExchangeObserver {
public:
    virtual ~ExchangeObserver() = default;
    // Before each input event is applied: `exch` is the state as of `now`, without that event.
    virtual void on_clock(Nanos, const MarketState&) {}
    virtual void on_exchange(const Event& e, ApplyResult r, const MarketState& exch, Nanos now) = 0;
    virtual void on_order(const OrderRequest&, const MarketState&, Nanos) {}
};

// Strategy side of our own orders. Every observer gets every update; filter on u.account.
class OrderObserver {
public:
    virtual ~OrderObserver() = default;
    virtual void on_order_update(const OrderUpdate& u, const MarketState& view, Nanos now) = 0;
};

// Upper bound for either latency. The sweep tops out at 250 ms; anything near int64 range would
// overflow time + latency.
inline constexpr Nanos kMaxLatency = 60 * kSec;

// When a market event reaches the strategy view.
//   ExchangeTime  its exchange timestamp + md_latency, as if the feed sent every event the
//                 moment it happened. SABR fits arrive at their own time (fit time + compute).
//   AsReceived    when the recorder actually got it: the public 100ms feed delivers events
//                 tens to hundreds of ms after their timestamp, and that delay is kept.
//                 The recorded receive time already contains our network latency
//                 (recorded_latency), which is swapped for md_latency so latency can still be
//                 swept:  max(exchange time, recv_ns - recorded_latency) + md_latency.
//                 Fits must come from a cache cut on receive time. They arrive at fit time +
//                 compute, later by md_latency - recorded_latency when that is positive (their
//                 inputs would have arrived that much later); never earlier.
//                 This treats the recorder's clock and the exchange's as in step.
// Events without a receive time fall back to exchange time + md_latency.
//
// In both modes one instrument's book messages reach the view in the order they arrived, and
// so do its tickers: a message is never delivered before an earlier one of the same instrument
// and channel, even when its own stamp is older (stamps do go backwards in the feed). A book
// is a chain of changes, so delivering it by stamp instead would break the chain and leave the
// view without that book until the next snapshot.
enum class MdTiming : std::uint8_t { ExchangeTime, AsReceived };

struct LoopConfig {
    Nanos md_latency = 0;     // exchange -> strategy
    Nanos order_latency = 0;  // strategy -> exchange
    MdTiming md_timing = MdTiming::ExchangeTime;
    Nanos recorded_latency = 0;  // AsReceived: network latency inside the recorded recv_ns
};

// Upper edges (ms) of the lateness buckets; the last bucket is everything above.
inline constexpr std::array<int, 6> kLateEdgesMs = {1, 10, 50, 100, 250, 1000};
using LateHist = std::array<std::int64_t, kLateEdgesMs.size() + 1>;

struct LoopStats {
    std::int64_t events = 0, books = 0, snapshots = 0, gaps = 0, no_snapshot = 0;
    std::int64_t trades = 0, tickers = 0, index = 0, fits = 0, defs = 0, recorder = 0;
    std::int64_t late_events = 0;  // exchange time behind the clock (cross-channel reordering)
    std::int64_t view_gaps = 0;    // book messages the strategy view could not chain
    // How far behind the clock late events are, all events and trades alone (fills depend on
    // the trades).
    std::int64_t late_trades = 0;
    LateHist late_hist{}, late_trade_hist{};
    Nanos max_late = 0, max_late_trade = 0;
};

class EventLoop {
public:
    // Latencies (recorded_latency too) must be in [0, kMaxLatency]; a negative one would be
    // look-ahead.
    explicit EventLoop(LoopConfig cfg);

    void add_market_observer(MarketObserver* o) { market_obs_.push_back(o); }
    void add_exchange_observer(ExchangeObserver* o) { exch_obs_.push_back(o); }
    void add_order_observer(OrderObserver* o) { order_obs_.push_back(o); }

    void process(Event e);
    // End of input: deliver everything still queued.
    void finish();
    // Called by strategies (from on_market / on_order_update): arrives at the exchange side at
    // now + order_latency.
    void submit_order(const OrderRequest& o);
    // Called by the exchange side: reaches order observers at u.exch_time + md_latency.
    void notify(const OrderUpdate& u);

    Nanos now() const { return now_; }
    const LoopConfig& config() const { return cfg_; }
    const LoopStats& stats() const { return stats_; }
    // Exchange-side state, for exchange observers and tests. Strategies must use the view.
    const MarketState& exchange() const { return exch_; }
    const MarketState& view() const { return view_; }

private:
    void release_until(Nanos t);
    void count(const Event& e, ApplyResult r);
    // Latest view release time given to this instrument on one channel; raises `release` to it.
    static Nanos in_arrival_order(std::vector<Nanos>& last, InstrumentId id, Nanos release);

    LoopConfig cfg_;
    Nanos now_ = 0;
    std::uint64_t seq_ = 0;
    MarketState exch_, view_;
    DelayQueue<Event> md_q_;
    DelayQueue<OrderRequest> order_q_;
    DelayQueue<OrderUpdate> update_q_;
    std::vector<Nanos> book_release_, ticker_release_;  // by instrument
    std::vector<MarketObserver*> market_obs_;
    std::vector<ExchangeObserver*> exch_obs_;
    std::vector<OrderObserver*> order_obs_;
    LoopStats stats_;
};

}  // namespace mm
