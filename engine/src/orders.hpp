// Our (simulated) orders: what a strategy sends to the exchange side, and what comes back.
// Both directions travel through the event loop's latency queues.
#pragma once

#include <cstdint>

#include "types.hpp"

namespace mm {

enum class Side : std::uint8_t { Buy, Sell };
inline double sign(Side s) { return s == Side::Buy ? 1.0 : -1.0; }

enum class OrderAction : std::uint8_t { Place, Cancel, Replace };

// PostOnly: a maker quote; rejected if it would cross the book on arrival, so it never takes.
// Ioc: a taker order; fills against the recorded book up to its limit price, the rest is dropped.
enum class OrderType : std::uint8_t { PostOnly, Ioc };

struct OrderRequest {
    std::uint32_t account = 0;
    std::uint64_t client_id = 0;  // Place: the new order. Cancel / Replace: the order acted on.
    OrderAction action = OrderAction::Place;
    OrderType type = OrderType::PostOnly;
    InstrumentId instrument = -1;
    Side side = Side::Buy;
    Px price = 0;              // limit price (Ioc: worst price accepted)
    double amount = 0;         // contracts (options) or USD (perpetual)
    std::uint64_t new_id = 0;  // Replace: id of the replacement order
};

enum class UpdateKind : std::uint8_t { Accepted, Rejected, Cancelled, Fill, Settled };
enum class Reject : std::uint8_t { None, WouldCross, NoBook, UnknownOrder, BadRequest, Expired };

// Exchange -> strategy. Delivered exch_time + md_latency later, like market data.
struct OrderUpdate {
    std::uint32_t account = 0;
    std::uint64_t client_id = 0;
    UpdateKind kind = UpdateKind::Accepted;
    Reject reason = Reject::None;
    InstrumentId instrument = -1;
    Side side = Side::Buy;
    Px price = 0;          // Accepted: resting price. Fill: fill price.
    double amount = 0;     // Fill: amount filled now. Settled: unused.
    double remaining = 0;  // amount still resting after this update
    Nanos exch_time = 0;
};

inline const char* to_string(Reject r) {
    switch (r) {
        case Reject::None: return "none";
        case Reject::WouldCross: return "would_cross";
        case Reject::NoBook: return "no_book";
        case Reject::UnknownOrder: return "unknown_order";
        case Reject::BadRequest: return "bad_request";
        case Reject::Expired: return "expired";
    }
    return "?";
}

}  // namespace mm
