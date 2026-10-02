// Event types produced from recorded (or live) Deribit messages and the SABR fit input.
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "types.hpp"

namespace mm {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

enum class BookAction : std::uint8_t { New, Change, Delete };

struct BookEntry {
    BookAction action;
    Px price;
    double amount;
};

// book.{instrument}.100ms: a snapshot, or the net changes of one 100ms batch.
struct BookUpdate {
    InstrumentId id = -1;
    bool snapshot = false;
    std::int64_t change_id = 0;
    std::int64_t prev_change_id = -1;  // -1 when absent (snapshots)
    std::vector<BookEntry> bids, asks;
};

struct Trade {
    InstrumentId id = -1;
    Px price = 0;
    double amount = 0;
    bool buy = false;  // aggressor side
    std::int64_t trade_seq = 0;
    double iv = kNaN;  // options only
    double mark_price = kNaN;
    double index_price = kNaN;
};

// ticker.{instrument}.100ms. Prices as doubles: this is analytics input, never a book key.
struct Ticker {
    InstrumentId id = -1;
    double mark_price = kNaN, mark_iv = kNaN, bid_iv = kNaN, ask_iv = kNaN;
    double underlying_price = kNaN, index_price = kNaN;
    double best_bid = kNaN, best_ask = kNaN, best_bid_amount = kNaN, best_ask_amount = kNaN;
    double delta = kNaN, gamma = kNaN, vega = kNaN, theta = kNaN;
};

struct IndexPrice {
    double price = kNaN;
};

// One line of cache/sabr/DAY.jsonl (written by sabr_sidecar, M3). Visible at fit_time + compute_ns.
struct SabrFit {
    Nanos fit_time = 0;
    Nanos compute_ns = 0;
    Nanos expiry = 0;
    double beta = kNaN, alpha = kNaN, rho = kNaN, nu = kNaN, forward = kNaN, rmse_vol = kNaN;
};

enum class InstrumentKind : std::uint8_t { Option, Perpetual, Other };

// From recorded public/get_instruments and public/get_instrument responses.
struct InstrumentDef {
    InstrumentId id = -1;
    std::string name;
    InstrumentKind kind = InstrumentKind::Other;
    bool is_call = false;
    double strike = kNaN;
    Nanos expiry = 0;
    double tick_size = kNaN;
    std::vector<std::pair<double, double>> tick_steps;  // (above_price, tick_size)
    double contract_size = kNaN;
    double min_trade_amount = kNaN;
    std::string settlement_period;  // "day" / "week" / "month" / "perpetual"
    double maker_fee = kNaN, taker_fee = kNaN;
};

// Lines the recorder writes about itself ({"event":{...}}).
struct RecorderEvent {
    enum class Kind : std::uint8_t { Connect, Disconnect, Gap, Resubscribe, Universe, Other };
    Kind kind = Kind::Other;
};

using Payload =
    std::variant<BookUpdate, Trade, Ticker, IndexPrice, SabrFit, InstrumentDef, RecorderEvent>;

struct Event {
    Nanos time = 0;     // exchange time; 0 = no exchange time (recorder events): use the clock
    Nanos recv_ns = 0;  // local receive time from the recorder line
    Payload payload;
};

}  // namespace mm
