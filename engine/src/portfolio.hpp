// An account's positions as known on the strategy side (from fills reported so far), and their
// Greeks from the strategy view. Used by the hedge runner.
#pragma once

#include <vector>

#include "market_state.hpp"

namespace mm {

// Deribit's conventions: delta in BTC (options: Black delta minus premium in BTC; perpetual:
// USD position / price), gamma per USD (options only), vega in USD per vol point.
struct PortfolioGreeks {
    double delta = 0, gamma = 0, vega = 0;
    double perp_price = kNaN;  // USD, NaN if no perpetual price is known
};

class Portfolio {
public:
    void add(InstrumentId id, double signed_amount);
    void clear(InstrumentId id);
    double position(InstrumentId id) const;

    // Options: Black-76 on the ticker's mark IV and underlying. Perpetual (`perp`, -1 = none):
    // the book mid, else the ticker's mark price. Positions that cannot be valued are skipped.
    PortfolioGreeks greeks(const MarketState& view, InstrumentId perp, Nanos now) const;

private:
    std::vector<double> pos_;  // by instrument
};

// Mid of a valid two-sided book, else the ticker's mark price, else NaN. As quoted.
double mark_price(const MarketState& state, InstrumentId id);

}  // namespace mm
