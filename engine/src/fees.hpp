// Deribit fee schedule, read from config/fees.json. All fees are returned in BTC.
#pragma once

#include <map>
#include <string>

namespace mm {

struct FeeSchedule {
    std::string date_checked, tier;
    double option_maker = 0, option_taker = 0;
    double option_cap = 0;  // fee never above this fraction of the option price
    double perp_maker = 0, perp_taker = 0;
    struct Delivery {
        double rate = 0;
        double cap = 0;  // fraction of the option's value; 0 = no cap in the schedule
    };
    std::map<std::string, Delivery> delivery;  // by the instrument's settlement_period
    std::string raw_json;                      // the file as read, echoed into config.json

    // Throws std::runtime_error if the file is missing or a field is absent.
    static FeeSchedule load(const std::string& path);

    // min(rate, cap * price) per contract. price in BTC, amount in contracts.
    double option_fee(double price_btc, double amount, bool maker) const;
    // rate * notional in BTC. price in USD, amount in USD.
    double perp_fee(double price_usd, double amount_usd, bool maker) const;
    // Charged on options that settle with a position. value_btc = settlement value per contract.
    // Throws std::out_of_range for a settlement period the schedule does not list.
    double delivery_fee(const std::string& settlement_period, double value_btc, double amount) const;
};

}  // namespace mm
