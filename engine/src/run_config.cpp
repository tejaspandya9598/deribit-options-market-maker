#include "run_config.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace mm {

using json = nlohmann::json;

namespace {

Nanos to_nanos(double value, Nanos unit) {
    return static_cast<Nanos>(std::llround(value * static_cast<double>(unit)));
}

void need(bool ok, const std::string& msg) {
    if (!ok) throw std::invalid_argument(msg);
}

QuoterConfig read_quoter(const json& j, QuoterConfig q, const std::string& where) {
    check_keys(j, {"size", "max_position", "max_vega_usd_per_vp", "min_abs_delta", "max_abs_delta",
                   "stale_after_s", "no_quote_before_expiry_s", "requote_interval_ms", "fit_max_age_s"},
               where);
    if (j.is_null()) return q;
    q.size = param(j, "size", q.size);
    q.max_position = param(j, "max_position", q.max_position);
    q.max_vega = param(j, "max_vega_usd_per_vp", q.max_vega);
    q.min_abs_delta = param(j, "min_abs_delta", q.min_abs_delta);
    q.max_abs_delta = param(j, "max_abs_delta", q.max_abs_delta);
    q.stale_after = to_nanos(param(j, "stale_after_s", static_cast<double>(q.stale_after) / kSec), kSec);
    q.no_quote_before_expiry =
        to_nanos(param(j, "no_quote_before_expiry_s", static_cast<double>(q.no_quote_before_expiry) / kSec), kSec);
    q.requote_interval =
        to_nanos(param(j, "requote_interval_ms", static_cast<double>(q.requote_interval) / kMs), kMs);
    q.fit_max_age = to_nanos(param(j, "fit_max_age_s", static_cast<double>(q.fit_max_age) / kSec), kSec);

    need(q.size > 0, where + ": size must be > 0");
    need(q.max_position >= q.size, where + ": max_position must be at least size");
    need(q.max_vega > 0, where + ": max_vega_usd_per_vp must be > 0");
    need(q.min_abs_delta >= 0 && q.min_abs_delta <= q.max_abs_delta && q.max_abs_delta <= 1,
         where + ": need 0 <= min_abs_delta <= max_abs_delta <= 1");
    need(q.stale_after > 0 && q.no_quote_before_expiry >= 0 && q.requote_interval >= 0 && q.fit_max_age > 0,
         where + ": times must be positive");
    return q;
}

}  // namespace

RunConfig RunConfig::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open run config " + path);
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) throw std::runtime_error("run config " + path + " is not valid JSON");
    return parse(j);
}

RunConfig RunConfig::parse(const json& j) {
    check_keys(j, {"inputs", "fill_model", "trade_through", "late_trade_window_ms", "pnl_interval_s",
                   "settle_wait_s", "starting_equity_btc", "quoter", "accounts"},
               "run config");
    need(j.is_object(), "run config must be an object");
    RunConfig c;
    try {
        if (auto it = j.find("inputs"); it != j.end()) c.inputs = it->get<std::vector<std::string>>();

        const auto model = j.value("fill_model", std::string("queue_aware"));
        need(model == "queue_aware" || model == "optimistic", "fill_model must be queue_aware or optimistic");
        c.sim.fill_model = model == "optimistic" ? FillModelKind::Optimistic : FillModelKind::QueueAware;

        const auto through = j.value("trade_through", std::string("trade_size"));
        need(through == "trade_size" || through == "full", "trade_through must be trade_size or full");
        c.sim.trade_through = through == "full" ? TradeThrough::Full : TradeThrough::TradeSize;

        c.sim.late_window = to_nanos(param(j, "late_trade_window_ms", static_cast<double>(c.sim.late_window) / kMs), kMs);
        c.sim.pnl_interval = to_nanos(param(j, "pnl_interval_s", static_cast<double>(c.sim.pnl_interval) / kSec), kSec);
        c.sim.settle_wait = to_nanos(param(j, "settle_wait_s", static_cast<double>(c.sim.settle_wait) / kSec), kSec);
        c.sim.starting_equity = param(j, "starting_equity_btc", c.sim.starting_equity);
        need(c.sim.late_window >= 0, "late_trade_window_ms must be >= 0");
        need(c.sim.pnl_interval > 0, "pnl_interval_s must be > 0");
        need(c.sim.settle_wait >= 0, "settle_wait_s must be >= 0");
        need(std::isfinite(c.sim.starting_equity), "starting_equity_btc must be a number");

        const QuoterConfig base = read_quoter(j.value("quoter", json()), QuoterConfig{}, "quoter");

        need(j.contains("accounts") && j["accounts"].is_array() && !j["accounts"].empty(),
             "run config needs a non-empty accounts list");
        std::set<std::string> names;
        for (const auto& a : j["accounts"]) {
            check_keys(a, {"name", "strategy", "params", "hedger", "quoter"}, "account");
            AccountConfig ac;
            ac.name = a.at("name").get<std::string>();
            ac.strategy = a.at("strategy").get<std::string>();
            ac.params = a.value("params", json::object());
            ac.hedger = a.value("hedger", std::string("none"));
            ac.quoter = read_quoter(a.value("quoter", json()), base, "account " + ac.name + " quoter");

            const bool safe = !ac.name.empty() &&
                              ac.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                                  std::string::npos;
            need(safe, "account name '" + ac.name + "' may only use letters, digits, '_' and '-'");
            need(names.insert(ac.name).second, "account name '" + ac.name + "' is used twice");
            // Hedgers arrive in M5.
            need(ac.hedger == "none", "account " + ac.name + ": hedger '" + ac.hedger + "' is not available yet (only \"none\")");
            c.accounts.push_back(std::move(ac));
        }
    } catch (const json::exception& e) {
        throw std::invalid_argument(std::string("run config: ") + e.what());
    }
    return c;
}

nlohmann::ordered_json to_json(const QuoterConfig& q) {
    return {
        {"size", q.size},
        {"max_position", q.max_position},
        {"max_vega_usd_per_vp", q.max_vega},
        {"min_abs_delta", q.min_abs_delta},
        {"max_abs_delta", q.max_abs_delta},
        {"stale_after_s", static_cast<double>(q.stale_after) / kSec},
        {"no_quote_before_expiry_s", static_cast<double>(q.no_quote_before_expiry) / kSec},
        {"requote_interval_ms", static_cast<double>(q.requote_interval) / kMs},
        {"fit_max_age_s", static_cast<double>(q.fit_max_age) / kSec},
    };
}

nlohmann::ordered_json to_json(const SimConfig& s) {
    return {
        {"fill_model", to_string(s.fill_model)},
        {"trade_through", to_string(s.trade_through)},
        {"late_trade_window_ms", static_cast<double>(s.late_window) / kMs},
        {"pnl_interval_s", static_cast<double>(s.pnl_interval) / kSec},
        {"settle_wait_s", static_cast<double>(s.settle_wait) / kSec},
        {"starting_equity_btc", s.starting_equity},
    };
}

}  // namespace mm
