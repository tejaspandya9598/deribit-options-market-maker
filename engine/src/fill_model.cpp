#include "fill_model.hpp"

#include <algorithm>

namespace mm {

void FillModel::on_join(Resting& o, double level_size) const {
    o.queue_ahead = level_size;
    o.level_size = level_size;
    o.trade_credit = 0;
    o.guess_cancel = 0;
    o.guess_cut = 0;
}

void FillModel::on_level(Resting& o, double new_size, Nanos t) const {
    // A trade whose book change never showed (e.g. the level was refilled in the same batch)
    // must not explain a drop much later.
    if (o.trade_credit > 0 && t - o.credit_time > late_window_) o.trade_credit = 0;

    if (new_size < o.level_size) {
        const double drop = o.level_size - new_size;
        const double by_trades = std::min(drop, o.trade_credit);  // already off the queue
        o.trade_credit -= by_trades;
        const double cancelled = drop - by_trades;
        if (cancelled > 0) {
            if (t - o.guess_time > late_window_) {
                o.guess_cancel = 0;
                o.guess_cut = 0;
            }
            const double cut = cancelled * o.queue_ahead / o.level_size;
            o.queue_ahead -= cut;
            o.guess_cancel += cancelled;
            o.guess_cut += cut;
            o.guess_time = t;
        }
    }
    o.level_size = new_size;
    // Never more ahead of us than is displayed.
    if (o.queue_ahead > new_size) {
        if (o.guess_cancel > 0) o.guess_cut += o.queue_ahead - new_size;
        o.queue_ahead = new_size;
    }
    if (o.queue_ahead < 0) o.queue_ahead = 0;
}

double FillModel::through_fill(const Resting& o, double avail) const {
    return through_ == TradeThrough::Full ? o.remaining : std::min(o.remaining, avail);
}

namespace {

class Optimistic final : public FillModel {
public:
    Optimistic(TradeThrough tt, Nanos w) : FillModel(tt, w) {}
    double on_trade(Resting& o, bool through, double, double avail, Nanos) const override {
        return through ? through_fill(o, avail) : std::min(o.remaining, avail);
    }
};

class QueueAware final : public FillModel {
public:
    QueueAware(TradeThrough tt, Nanos w) : FillModel(tt, w) {}
    double on_trade(Resting& o, bool through, double trade_amount, double avail,
                    Nanos t) const override {
        if (through) {
            // The print went past our price, so nothing was left ahead of us.
            o.queue_ahead = 0;
            return through_fill(o, avail);
        }
        // A level drop at or after this trade's time was taken as a cancel. If this print is
        // that drop arriving late, undo the cancel's cut; the trade is charged in full below.
        double seen = 0;
        if (o.guess_cancel > 0 && t <= o.guess_time) {
            seen = std::min(trade_amount, o.guess_cancel);
            const double restore = o.guess_cut * seen / o.guess_cancel;
            o.guess_cancel -= seen;
            o.guess_cut -= restore;
            o.queue_ahead += restore;
        }
        // The rest of the print has not shown in the book yet.
        if (trade_amount > seen) {
            o.trade_credit += trade_amount - seen;
            o.credit_time = t;
        }
        const double ahead = std::min(o.queue_ahead, trade_amount);
        o.queue_ahead -= ahead;
        return std::min(o.remaining, std::min(avail, trade_amount - ahead));
    }
};

}  // namespace

std::unique_ptr<FillModel> make_fill_model(FillModelKind kind, TradeThrough tt, Nanos late_window) {
    if (kind == FillModelKind::Optimistic) return std::make_unique<Optimistic>(tt, late_window);
    return std::make_unique<QueueAware>(tt, late_window);
}

const char* to_string(FillModelKind k) {
    return k == FillModelKind::Optimistic ? "optimistic" : "queue_aware";
}

const char* to_string(TradeThrough t) { return t == TradeThrough::TradeSize ? "trade_size" : "full"; }

}  // namespace mm
