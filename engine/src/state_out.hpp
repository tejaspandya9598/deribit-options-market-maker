// Engine state as JSON lines on stdout, for the dashboard: one line every `interval` of
// exchange time. This is the exchange-side truth (what a pnl row holds), not the strategy view.
// Read-only: a run writes the same files with or without it.
//
//   {"type":"state","t":<ns>,"index":<usd>,
//    "accounts":[{"name","equity","pnl":{"total","spread","fees","adverse","option_mtm","hedge"},
//                 "delta","gamma","vega","fills":<count so far>,
//                 "positions":[[instrument,quantity,mark]],
//                 "quotes":[[instrument,bid,bid_amount,ask,ask_amount]]}],        null = no quote
//    "fills":[{"a":<account index>,"t","i","side","px","amt","maker","fee"}],     new since last
//    "books":[[instrument,best_bid,best_ask,mark_iv]]}       instruments quoted or held, + perp
// Prices as quoted on Deribit (options in BTC, the perpetual in USD); PnL and equity in BTC.
#pragma once

#include <cstdio>
#include <vector>

#include "event_loop.hpp"
#include "exchange_sim.hpp"
#include "instruments.hpp"

namespace mm {

class StateWriter final : public ExchangeObserver {
public:
    StateWriter(std::FILE* out, const ExchangeSim& sim, const Instruments& reg, Nanos interval);

    void on_clock(Nanos now, const MarketState& exch) override;
    void on_exchange(const Event& e, ApplyResult r, const MarketState& exch, Nanos now) override;
    // A last state at the end of the run.
    void finish(const MarketState& exch, Nanos now);

private:
    void emit(const MarketState& exch, Nanos t);

    std::FILE* out_;
    const ExchangeSim& sim_;
    const Instruments& reg_;
    Nanos interval_;
    Nanos next_ = 0;
    bool started_ = false;
    InstrumentId perp_ = -1;
    std::vector<size_t> fills_seen_;  // per account
};

}  // namespace mm
