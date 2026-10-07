// The simulated exchange side: our resting orders, fills against recorded trades and books,
// fees, expiry settlement, and the paper accounts those feed.
//
// Recorded books are replayed as they were. Our orders never change them, and accounts do not
// see or compete with each other (both are stated limits, see SPEC 5).
//
// Time rules (events of different channels arrive up to a few hundred ms out of order):
//   - an order is live from the exchange time it arrived until the exchange time it was
//     cancelled; a trade fills it only if the trade's own timestamp falls in that span. So a
//     trade print that arrives late cannot fill an order placed after it, and can still fill an
//     order that was cancelled after it. Cancelled orders are kept for `late_window` for that.
//   - queue position is taken from the book as known when the order arrives.
//   - a maker fill's mid (for its edge and the spread / adverse split) is the mid at the
//     trade's own timestamp, not the book as it stands when a late print finally arrives.
//     Two short histories per instrument give it: book mids and ticker mark prices, each
//     keyed by its own channel's timestamps. Mid at t = the book mid in effect at t, or, if
//     the book was one-sided or invalid at t, the ticker mark in effect at t.
//     Stamps go backwards in the recording, across channels and within one. The rule, the
//     same one the market state uses: the message that arrived last is the channel's current
//     state, and in the history it is in effect from its own stamp, replacing any entries
//     stamped at or after it. (The ticker timestamp's exact meaning is not documented; if a
//     later-arriving, older-stamped ticker is really stale data, this rule is wrong for it.)
// Maker fills are rounded down to whole lots (the instrument's min_trade_amount).
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "account.hpp"
#include "delay_queue.hpp"
#include "event_loop.hpp"
#include "fees.hpp"
#include "fill_model.hpp"

namespace mm {

struct SimConfig {
    FillModelKind fill_model = FillModelKind::QueueAware;
    TradeThrough trade_through = TradeThrough::TradeSize;
    Nanos late_window = 1 * kSec;   // see above
    Nanos pnl_interval = 10 * kSec;  // spacing of pnl rows on the exchange clock
    Nanos settle_wait = 60 * kSec;   // how long to wait for Deribit's final delivery price
    double starting_equity = 1.0;    // BTC, every account
};

struct PnlRow {
    Nanos time = 0;
    double equity = 0, cash = 0;  // BTC
    PnlParts pnl;
    // Deribit's conventions: delta in BTC (options: Black delta minus premium), gamma per USD,
    // vega in USD per vol point. From Black-76 on the ticker's mark IV and underlying.
    double delta = 0, gamma = 0, vega = 0;
    int positions = 0;
    double index = kNaN;  // USD, for converting BTC figures in reports
};

// Receives the pnl time series. Rows: one per account at the start and the end, one after
// every fill and settlement, and one every pnl_interval, except that interval rows are skipped
// while an account is flat and nothing changed (they would repeat the previous row).
class RunSink {
public:
    virtual ~RunSink() = default;
    virtual void pnl_row(size_t account, const PnlRow& row) = 0;
};

struct AccountStats {
    std::int64_t placed = 0, replaced = 0, cancelled = 0;
    std::int64_t rejected_cross = 0, rejected_other = 0;
    std::int64_t maker_fills = 0, taker_fills = 0;
    std::int64_t settlements = 0;  // option positions carried into expiry
    double delivery_fees = 0;      // BTC
};

class ExchangeSim final : public ExchangeObserver {
public:
    ExchangeSim(EventLoop& loop, const FeeSchedule& fees, SimConfig cfg,
                const std::vector<std::string>& account_names, RunSink* sink = nullptr);

    void on_clock(Nanos now, const MarketState& exch) override;
    void on_exchange(const Event& e, ApplyResult r, const MarketState& exch, Nanos now) override;
    void on_order(const OrderRequest& o, const MarketState& exch, Nanos now) override;
    // After EventLoop::finish(): last marks and the closing pnl rows.
    void finish();

    size_t accounts() const { return accounts_.size(); }
    const Account& account(size_t i) const { return accounts_.at(i); }
    const AccountStats& stats(size_t i) const { return stats_.at(i); }
    const SimConfig& config() const { return cfg_; }
    // Our orders on one instrument, including cancelled ones still inside late_window.
    const std::vector<Resting>& resting(InstrumentId id) const;
    // Settlements by price source: Deribit's final delivery_price, or (if that ticker never
    // came within settle_wait) the last estimated_delivery_price / index before expiry.
    std::int64_t settled_final() const { return settled_final_; }
    std::int64_t settled_estimate() const { return settled_estimate_; }

    // Unit value (see account.hpp) from the exchange-side state: book mid if the book is valid
    // and two-sided, else the ticker's mark price, else the last value; frozen at expiry.
    double unit_value(InstrumentId id, const MarketState& exch);

private:
    struct Instr {
        bool registered = false;  // expiry scheduled
        bool expired = false;
        bool awaiting = false;    // expired, final delivery price not in yet
        Nanos settle_deadline = 0;
        double est_delivery = kNaN;
        Nanos ticker_time = 0;  // exchange time of the latest ticker
        double last_u = kNaN;
        double frozen_u = kNaN;
    };
    struct Markout {
        std::uint32_t account;
        size_t fill;
        size_t horizon;
        InstrumentId instrument;
    };
    struct MidPoint {
        Nanos time;  // exchange time the value took effect
        double px;   // as quoted; NaN = none (book one-sided or invalid, no mark price)
    };
    struct MidHistory {
        std::deque<MidPoint> book, mark;
    };
    struct RowState {
        bool written = false, flat = true;
        double cash = 0;
    };

    Instr& instr(InstrumentId id);
    std::vector<Resting>& orders(InstrumentId id);
    void advance(Nanos now, const MarketState& exch);
    void purge(InstrumentId id, Nanos now);
    bool has_orders(InstrumentId id) const;
    // Mid history, kept only for instruments we have orders on. note_book / note_mark record
    // the channel's current value at its own latest timestamp; mid_at returns the unit value
    // in effect at t.
    void note(std::deque<MidPoint>& h, Nanos t, double px, Nanos now);
    void note_book(InstrumentId id, const MarketState& exch, Nanos now);
    void note_mark(InstrumentId id, const MarketState& exch, Nanos now);
    double mid_at(InstrumentId id, Nanos t, const MarketState& exch);
    void send(const OrderRequest& o, std::uint64_t id, UpdateKind kind, Reject reason, Px price,
              double amount, double remaining, Nanos t);
    // Checks shared by Place and Replace. Reject::None = can rest.
    Reject check_post_only(InstrumentId id, Side side, Px price, double amount,
                           const MarketState& exch, Nanos now);
    void place(const OrderRequest& o, const MarketState& exch, Nanos now);
    void take(const OrderRequest& o, const MarketState& exch, Nanos now);
    void cancel(const OrderRequest& o, Nanos now);
    void replace(const OrderRequest& o, const MarketState& exch, Nanos now);
    void on_book(const BookUpdate& u, ApplyResult r, Nanos t, const MarketState& exch);
    void on_trade(const Trade& tr, Nanos t, const MarketState& exch, Nanos now);
    void apply_fill(std::uint32_t account, std::uint64_t client_id, InstrumentId id, Side side,
                    Px price, double amount, double remaining, bool maker, Nanos t,
                    const MarketState& exch, Nanos now);
    void expire(InstrumentId id, Nanos expiry, const MarketState& exch);
    void settle(InstrumentId id, double delivery_price, bool final_price, const MarketState& exch,
                Nanos now);
    void write_row(size_t account, const MarketState& exch, Nanos time, bool force);

    EventLoop& loop_;
    const FeeSchedule& fees_;
    SimConfig cfg_;
    std::unique_ptr<FillModel> model_;
    RunSink* sink_;
    std::vector<Account> accounts_;
    std::vector<AccountStats> stats_;
    std::vector<RowState> rows_;
    std::vector<std::vector<Resting>> orders_;  // by instrument
    std::vector<Instr> instr_;
    std::vector<MidHistory> mids_;  // by instrument
    std::map<Nanos, std::vector<InstrumentId>> expiries_;
    std::vector<InstrumentId> awaiting_;
    DelayQueue<Markout> timers_;
    std::uint64_t timer_seq_ = 0;
    bool started_ = false;  // first book message seen: pnl rows start here
    Nanos next_sample_ = 0;
    std::int64_t settled_final_ = 0, settled_estimate_ = 0;
};

}  // namespace mm
