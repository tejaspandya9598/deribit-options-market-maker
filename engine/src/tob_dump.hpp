// Exchange-side top of book after every book message, as integer CSV. check_book.py `tob`
// writes the same format from the raw files, so the two can be compared byte for byte.
//
//   recv_ns,instrument,change_id,valid,bid_px_e8,bid_amt_e8,ask_px_e8,ask_amt_e8
//
// Prices and amounts are x 1e8 as integers (to_e8). Empty fields when the book is invalid or a
// side is empty.
#pragma once

#include <cstdio>

#include "event_loop.hpp"
#include "instruments.hpp"

namespace mm {

class TobDump : public ExchangeObserver {
public:
    TobDump(std::FILE* out, const Instruments& reg);
    void on_exchange(const Event& e, ApplyResult r, const MarketState& exch, Nanos now) override;

private:
    std::FILE* out_;
    const Instruments& reg_;
};

}  // namespace mm
