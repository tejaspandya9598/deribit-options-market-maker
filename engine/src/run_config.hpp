// Run config: which accounts (strategy + parameters + hedger) run side by side, and the
// simulation settings they share. Read from a JSON file (see config/run_m4.json).
//
// {
//   "inputs": ["..."],                 what is piped into stdin; free text, echoed for the record
//   "fill_model": "queue_aware",       or "optimistic"
//   "trade_through": "trade_size",     or "full" (see fill_model.hpp)
//   "late_trade_window_ms": 1000,
//   "pnl_interval_s": 10,
//   "settle_wait_s": 60,
//   "starting_equity_btc": 1.0,
//   "quoter": { ... },                 defaults for every account (see quoter.hpp)
//   "accounts": [
//     {"name": "...", "strategy": "...", "params": { ... }, "hedger": "none", "quoter": { ... }}
//   ]
// }
// Unknown keys are errors, so a typo cannot silently run with a default.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "exchange_sim.hpp"
#include "quoter.hpp"

namespace mm {

struct AccountConfig {
    std::string name;  // also the output folder: letters, digits, '_' and '-' only
    std::string strategy;
    nlohmann::json params = nlohmann::json::object();
    std::string hedger = "none";
    QuoterConfig quoter;
};

struct RunConfig {
    std::vector<std::string> inputs;
    SimConfig sim;
    std::vector<AccountConfig> accounts;

    // Throws std::runtime_error / std::invalid_argument with a message naming the problem.
    static RunConfig load(const std::string& path);
    static RunConfig parse(const nlohmann::json& j);
};

nlohmann::ordered_json to_json(const QuoterConfig& q);
nlohmann::ordered_json to_json(const SimConfig& s);

}  // namespace mm
