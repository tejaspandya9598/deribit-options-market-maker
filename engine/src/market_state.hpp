// Books, tickers, index, instrument data and latest SABR fits, built only from applied events.
// The engine keeps two: the exchange-side state (events applied at their exchange time) and the
// strategy view (the same events, delivered after the market-data latency).
#pragma once

#include <map>
#include <optional>
#include <vector>

#include "events.hpp"
#include "order_book.hpp"

namespace mm {

class MarketState {
public:
    // BookUpdate -> the book's result; every other event -> NotBook.
    ApplyResult apply(const Event& e);

    const OrderBook* book(InstrumentId id) const;
    const Ticker* ticker(InstrumentId id) const;
    const InstrumentDef* def(InstrumentId id) const;
    double index_price() const { return index_; }
    const std::map<Nanos, SabrFit>& fits() const { return fits_; }  // by expiry
    const std::vector<OrderBook>& books() const { return books_; }

private:
    template <class T>
    static T& slot(std::vector<T>& v, InstrumentId id);

    std::vector<OrderBook> books_;
    std::vector<std::optional<Ticker>> tickers_;
    std::vector<std::optional<InstrumentDef>> defs_;
    double index_ = kNaN;
    std::map<Nanos, SabrFit> fits_;
};

}  // namespace mm
