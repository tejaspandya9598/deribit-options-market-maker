#include "hedger.hpp"

#include <cmath>
#include <map>
#include <stdexcept>

#include "strategy.hpp"  // param(), check_keys()

namespace mm {

double whalley_wilmott_band(double cost_rate, double perp_price, double gamma, double risk_aversion) {
    return std::cbrt(1.5 * cost_rate * perp_price * gamma * gamma / risk_aversion);
}

namespace {

class NoHedge final : public Hedger {
public:
    double trade_delta(const HedgeContext&) override { return 0.0; }
    nlohmann::json params() const override { return nlohmann::json::object(); }
};

class FixedBand final : public Hedger {
public:
    explicit FixedBand(double band) : band_(band) {}
    double trade_delta(const HedgeContext& c) override { return std::abs(c.delta) > band_ ? -c.delta : 0.0; }
    nlohmann::json params() const override { return {{"band_btc", band_}}; }

private:
    double band_;
};

class TimeBased final : public Hedger {
public:
    explicit TimeBased(double minutes)
        : minutes_(minutes), interval_(static_cast<Nanos>(std::llround(minutes * 60.0 * kSec))) {}
    double trade_delta(const HedgeContext& c) override {
        if (next_ == 0) {  // first look: wait for the next boundary
            next_ = (c.now / interval_ + 1) * interval_;
            return 0.0;
        }
        if (c.now < next_) return 0.0;
        next_ = (c.now / interval_ + 1) * interval_;
        return -c.delta;
    }
    nlohmann::json params() const override { return {{"interval_min", minutes_}}; }

private:
    double minutes_;
    Nanos interval_;
    Nanos next_ = 0;
};

class WhalleyWilmott final : public Hedger {
public:
    WhalleyWilmott(double risk_aversion, double min_band) : risk_aversion_(risk_aversion), min_band_(min_band) {}
    double trade_delta(const HedgeContext& c) override {
        const double band =
            std::max(min_band_, whalley_wilmott_band(c.cost_rate, c.perp_price, c.gamma, risk_aversion_));
        if (c.delta > band) return band - c.delta;
        if (c.delta < -band) return -band - c.delta;
        return 0.0;
    }
    nlohmann::json params() const override {
        return {{"risk_aversion_per_usd", risk_aversion_}, {"min_band_btc", min_band_}};
    }

private:
    double risk_aversion_, min_band_;
};

std::map<std::string, HedgerFactory>& registry() {
    static std::map<std::string, HedgerFactory> r;
    return r;
}

}  // namespace

void register_hedger(const std::string& name, HedgerFactory factory) { registry()[name] = std::move(factory); }

void register_builtin_hedgers() {
    register_hedger("none", [](const nlohmann::json& p) -> std::unique_ptr<Hedger> {
        check_keys(p, {}, "none hedger params");
        return std::make_unique<NoHedge>();
    });
    register_hedger("fixed_band", [](const nlohmann::json& p) -> std::unique_ptr<Hedger> {
        check_keys(p, {"band_btc"}, "fixed_band params");
        const double band = param(p, "band_btc", 0.05);
        if (!(band > 0)) throw std::invalid_argument("fixed_band: band_btc must be > 0");
        return std::make_unique<FixedBand>(band);
    });
    register_hedger("time_based", [](const nlohmann::json& p) -> std::unique_ptr<Hedger> {
        check_keys(p, {"interval_min"}, "time_based params");
        const double minutes = param(p, "interval_min", 15.0);
        if (!(minutes > 0)) throw std::invalid_argument("time_based: interval_min must be > 0");
        return std::make_unique<TimeBased>(minutes);
    });
    register_hedger("whalley_wilmott", [](const nlohmann::json& p) -> std::unique_ptr<Hedger> {
        check_keys(p, {"risk_aversion_per_usd", "min_band_btc"}, "whalley_wilmott params");
        const double ra = param(p, "risk_aversion_per_usd", 1e-3);
        const double min_band = param(p, "min_band_btc", 0.005);
        if (!(ra > 0)) throw std::invalid_argument("whalley_wilmott: risk_aversion_per_usd must be > 0");
        if (min_band < 0) throw std::invalid_argument("whalley_wilmott: min_band_btc must be >= 0");
        return std::make_unique<WhalleyWilmott>(ra, min_band);
    });
}

std::unique_ptr<Hedger> make_hedger(const std::string& name, const nlohmann::json& params) {
    auto it = registry().find(name);
    if (it == registry().end()) throw std::invalid_argument("unknown hedger '" + name + "'");
    return it->second(params);
}

}  // namespace mm
