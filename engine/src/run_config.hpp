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
//   "trade_from_utc": "2026-09-30T21:42:41Z",   optional: nothing is quoted or hedged before
//   "trade_until_utc": "2026-10-05T21:42:41Z",  optional: nor from here on; the run ends
//                                               shortly after (see kRunOff)
//   "hedge_interval_ms": 1000,         how often a hedger looks at the account's delta
//   "hedge_slippage": 0.001,           how far past the best price a hedge order may fill
//   "quoter": { ... },                 defaults for every account (see quoter.hpp)
//   "accounts": [
//     {"name": "...", "strategy": "...", "params": { ... },
//      "hedger": "none", "hedger_params": { ... }, "quoter": { ... }}
//   ]
// }
// Unknown keys are errors, so a typo cannot silently run with a default.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "exchange_sim.hpp"
#include "hedge_runner.hpp"
#include "quoter.hpp"

namespace mm {

struct AccountConfig {
    std::string name;  // also the output folder: letters, digits, '_' and '-' only
    std::string strategy;
    nlohmann::json params = nlohmann::json::object();
    std::string hedger = "none";
    nlohmann::json hedger_params = nlohmann::json::object();
    QuoterConfig quoter;
};

// After trade_until the run goes on this long, so the last fills get their 60 s markouts,
// then stops; positions still open are marked to mid there, not closed.
inline constexpr Nanos kRunOff = 70 * kSec;

struct RunConfig {
    std::vector<std::string> inputs;
    SimConfig sim;
    HedgeRunnerConfig hedge;
    Nanos trade_from = 0, trade_until = 0;  // 0 = no bound
    std::string trade_from_utc, trade_until_utc;  // as given, for config.json
    std::vector<AccountConfig> accounts;

    // Throws std::runtime_error / std::invalid_argument with a message naming the problem.
    static RunConfig load(const std::string& path);
    static RunConfig parse(const nlohmann::json& j);
};

nlohmann::ordered_json to_json(const QuoterConfig& q);
nlohmann::ordered_json to_json(const SimConfig& s);

// "YYYY-MM-DDTHH:MM:SSZ" (UTC) -> nanoseconds. Throws std::invalid_argument otherwise.
Nanos parse_utc(const std::string& s);

}  // namespace mm
