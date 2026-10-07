// One per hedged account: watches the account's delta on the strategy view and sends the
// hedger's trades to BTC-PERPETUAL as immediate-or-cancel taker orders.
//
//   - delta comes from fills reported so far and the view's tickers (no look-ahead)
//   - checked at most once per `interval`, on perpetual market data
//   - one hedge order at a time: the next check waits for the exchange's answer, or for
//     answer_timeout plus two round trips if none comes
//   - size is rounded to whole perpetual lots (min_trade_amount, 10 USD); smaller is skipped
//   - the limit price allows at most `slippage` past the visible best price (rounded to the
//     tick towards the best price); what the recorded book cannot fill inside it is dropped
//   - idle before trade_from and from trade_until on
#pragma once

#include <cstdint>
#include <memory>

#include "event_loop.hpp"
#include "fees.hpp"
#include "hedger.hpp"
#include "portfolio.hpp"

namespace mm {

struct HedgeRunnerConfig {
    Nanos interval = 1 * kSec;
    double slippage = 0.001;  // fraction of price
    Nanos answer_timeout = 5 * kSec;
    Nanos trade_from = 0, trade_until = 0;  // 0 = no bound
};

struct HedgeStats {
    std::int64_t checks = 0, orders = 0, fills = 0, unfilled = 0, rejected = 0;
    std::int64_t timed_out = 0;  // orders given up on because no answer came
};

class HedgeRunner final : public MarketObserver, public OrderObserver {
public:
    HedgeRunner(EventLoop& loop, std::uint32_t account, std::unique_ptr<Hedger> hedger,
                const FeeSchedule& fees, HedgeRunnerConfig cfg);

    void on_market(const Event& e, const MarketState& view, Nanos now) override;
    void on_order_update(const OrderUpdate& u, const MarketState& view, Nanos now) override;

    const Hedger& hedger() const { return *hedger_; }
    const HedgeStats& stats() const { return stats_; }
    const Portfolio& portfolio() const { return portfolio_; }

private:
    void check(const MarketState& view, Nanos now);

    EventLoop& loop_;
    std::uint32_t account_;
    std::unique_ptr<Hedger> hedger_;
    const FeeSchedule& fees_;
    HedgeRunnerConfig cfg_;
    HedgeStats stats_;
    Portfolio portfolio_;
    InstrumentId perp_ = -1;
    Nanos last_check_ = 0;
    static constexpr std::uint64_t kFirstHedgeId = std::uint64_t{1} << 62;  // apart from the quoter's ids
    std::uint64_t next_id_ = kFirstHedgeId;
    std::uint64_t pending_ = 0;                       // hedge order awaiting its answer
    Nanos pending_since_ = 0;
};

}  // namespace mm
