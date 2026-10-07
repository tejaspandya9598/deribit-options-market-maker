#include "fees.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace mm {

FeeSchedule FeeSchedule::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open fee schedule " + path);
    std::stringstream ss;
    ss << in.rdbuf();

    FeeSchedule f;
    f.raw_json = ss.str();
    try {
        auto j = nlohmann::json::parse(f.raw_json);
        f.date_checked = j.at("date_checked").get<std::string>();
        f.tier = j.at("tier").get<std::string>();
        const auto& opt = j.at("option");
        f.option_maker = opt.at("maker").get<double>();
        f.option_taker = opt.at("taker").get<double>();
        f.option_cap = opt.at("fee_cap_fraction_of_price").get<double>();
        const auto& perp = j.at("perpetual");
        f.perp_maker = perp.at("maker").get<double>();
        f.perp_taker = perp.at("taker").get<double>();
        for (const auto& [period, d] : j.at("option_delivery").at("by_settlement_period").items()) {
            Delivery x;
            x.rate = d.at("rate").get<double>();
            const auto& cap = d.at("cap_fraction_of_value");
            x.cap = cap.is_number() ? cap.get<double>() : 0.0;
            f.delivery[period] = x;
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("bad fee schedule " + path + ": " + e.what());
    }
    return f;
}

double FeeSchedule::option_fee(double price_btc, double amount, bool maker) const {
    const double rate = maker ? option_maker : option_taker;
    return std::min(rate, option_cap * price_btc) * std::abs(amount);
}

double FeeSchedule::perp_fee(double price_usd, double amount_usd, bool maker) const {
    const double rate = maker ? perp_maker : perp_taker;
    return rate * std::abs(amount_usd) / price_usd;
}

double FeeSchedule::delivery_fee(const std::string& settlement_period, double value_btc,
                                 double amount) const {
    const Delivery& d = delivery.at(settlement_period);
    const double per_contract = d.cap > 0 ? std::min(d.rate, d.cap * value_btc) : d.rate;
    return per_contract * std::abs(amount);
}

}  // namespace mm
