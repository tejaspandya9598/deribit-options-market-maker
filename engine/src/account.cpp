#include "account.hpp"

#include <cmath>
#include <utility>

namespace mm {

Account::Account(std::string name, double starting_equity)
    : name_(std::move(name)), start_(starting_equity), cash_(starting_equity) {}

Account::Position& Account::slot(InstrumentId id) {
    auto i = static_cast<size_t>(id);
    if (i >= pos_.size()) pos_.resize(i + 1);
    return pos_[i];
}

double Account::position(InstrumentId id) const {
    auto i = static_cast<size_t>(id);
    return i < pos_.size() ? pos_[i].qty : 0.0;
}

double Account::equity() const {
    double e = cash_;
    for (const auto& p : pos_) {
        if (p.qty != 0) e += p.qty * p.last_u;
    }
    return e;
}

void Account::mark(InstrumentId id, double u) {
    if (!std::isfinite(u)) return;
    Position& p = slot(id);
    if (std::isfinite(p.last_u)) {
        const double d = u - p.last_u;
        if (p.option) {
            pnl_.adverse += p.young * d;
            pnl_.option_mtm += (p.qty - p.young) * d;
        } else {
            pnl_.hedge += p.qty * d;
        }
    }
    p.last_u = u;
}

size_t Account::fill(FillRecord f) {
    Position& p = slot(f.instrument);
    p.option = f.option;
    if (!std::isfinite(f.u_mid)) f.u_mid = f.u_fill;
    mark(f.instrument, f.u_mid);

    const double q = sign(f.side) * f.amount;
    cash_ -= q * f.u_fill;
    cash_ -= f.fee;
    pnl_.fees -= f.fee;
    const double edge = q * (f.u_mid - f.u_fill);
    (f.option ? pnl_.spread : pnl_.hedge) += edge;

    p.qty += q;
    if (f.option) {
        p.young += q;
        f.young = true;
    }
    f.position_after = p.qty;
    fills_.push_back(f);
    return fills_.size() - 1;
}

void Account::markout(size_t fill_index, size_t horizon, double u_now) {
    FillRecord& f = fills_.at(fill_index);
    if (std::isfinite(u_now)) f.markout.at(horizon) = sign(f.side) * (u_now - f.u_fill);
    if (horizon == kAdverseHorizon && f.young) {
        mark(f.instrument, u_now);
        slot(f.instrument).young -= sign(f.side) * f.amount;
        f.young = false;
    }
}

void Account::settle(InstrumentId id, double u_settle, double fee) {
    mark(id, u_settle);
    Position& p = slot(id);
    for (auto& f : fills_) {
        if (f.instrument == id) f.young = false;
    }
    if (p.qty != 0) cash_ += p.qty * p.last_u;
    cash_ -= fee;
    pnl_.fees -= fee;
    p.qty = 0;
    p.young = 0;
}

}  // namespace mm
