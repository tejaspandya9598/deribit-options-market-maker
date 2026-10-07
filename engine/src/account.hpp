// One paper account: cash in BTC, positions, and PnL split into parts.
//
// Every instrument has a "unit value" u in BTC per unit of position:
//   option     u = price in BTC             (position in contracts, 1 BTC each)
//   perpetual  u = -1 / price in USD        (position in USD; the perpetual is inverse, so a
//                                            long of A USD from p0 to p1 earns A * (1/p0 - 1/p1))
// A fill of signed quantity q at u_fill moves cash by -q * u_fill, then the position is marked
// at the mid. So equity = cash + sum(qty * u) holds for both kinds with the same code.
//
// PnL parts (BTC). They are accumulated separately from cash and positions, and
//   equity - starting equity = spread + fees + adverse + option_mtm + hedge
//   spread      options: q * (u_mid - u_fill) at each fill (what the quote earned against mid)
//   fees        minus every trading and delivery fee
//   adverse     options: mid move in the first 60 s after each fill (adverse selection)
//   option_mtm  options: mid move after those 60 s, including settlement at expiry
//   hedge       perpetual: the same edge and mid moves, all in one part
// A fill's 60 s markout (mid 60 s later vs fill price) therefore equals its spread plus its
// adverse part.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "events.hpp"
#include "orders.hpp"

namespace mm {

inline constexpr std::array<Nanos, 3> kMarkoutHorizons = {1 * kSec, 10 * kSec, 60 * kSec};
inline constexpr size_t kAdverseHorizon = 2;  // the 60 s markout closes the adverse window

struct PnlParts {
    double spread = 0, fees = 0, adverse = 0, option_mtm = 0, hedge = 0;
    double total() const { return spread + fees + adverse + option_mtm + hedge; }
};

struct FillRecord {
    Nanos time = 0;  // exchange time of the trade that filled us (taker: of our order's arrival)
    InstrumentId instrument = -1;
    std::uint64_t client_id = 0;
    Side side = Side::Buy;
    bool option = true;
    bool maker = true;
    double price = 0;   // as quoted: BTC per contract (options), USD (perpetual)
    double amount = 0;  // contracts (options), USD (perpetual)
    double fee = 0;     // BTC
    double mid = kNaN;  // as quoted, at the fill; NaN = no mark known (edge counted as 0)
    double u_fill = 0, u_mid = kNaN;  // unit values, BTC
    double position_after = 0;
    // BTC per unit, mid at each horizon vs the fill price; positive = in our favour.
    std::array<double, kMarkoutHorizons.size()> markout{kNaN, kNaN, kNaN};
    bool young = false;  // still inside the adverse window
};

class Account {
public:
    struct Position {
        double qty = 0;
        double last_u = kNaN;  // unit value the position is marked at
        double young = 0;      // signed quantity of fills still inside the adverse window
        bool option = true;
    };

    Account(std::string name, double starting_equity);

    // Re-mark one instrument: the position's gain since the last mark goes to the PnL parts.
    // Ignores a non-finite u.
    void mark(InstrumentId id, double u);
    // Apply a fill (marks the instrument at f.u_mid first). Returns the fill's index in fills().
    size_t fill(FillRecord f);
    // Record one markout of a fill. u_now = the instrument's unit value at that horizon.
    void markout(size_t fill_index, size_t horizon, double u_now);
    // Expiry: the whole position turns into cash at u_settle, minus the delivery fee.
    void settle(InstrumentId id, double u_settle, double fee);

    const std::string& name() const { return name_; }
    double starting_equity() const { return start_; }
    double cash() const { return cash_; }
    // cash + positions at their last marks. Computed from scratch, not from the PnL parts.
    double equity() const;
    const PnlParts& pnl() const { return pnl_; }
    double position(InstrumentId id) const;
    const std::vector<Position>& positions() const { return pos_; }  // indexed by instrument id
    const std::vector<FillRecord>& fills() const { return fills_; }

private:
    Position& slot(InstrumentId id);

    std::string name_;
    double start_;
    double cash_;
    PnlParts pnl_;
    std::vector<Position> pos_;
    std::vector<FillRecord> fills_;
};

}  // namespace mm
