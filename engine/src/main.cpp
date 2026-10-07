// mm_engine: replays recorded (or live) Deribit data from stdin.
//
//   gunzip -c data/raw/DAY/*.jsonl.gz | mm_engine [options]
//
// Options:
//   --md-latency-ms X      exchange -> strategy latency
//   --order-latency-ms X   strategy -> exchange latency
//                          Default for both: lowest probe round trip in $DATA_DIR/logs / 2
//                          (best case measured). Exits if no probes and no flag.
//   --md-timing MODE       when a strategy sees market data (see MdTiming in event_loop.hpp):
//                            received  when the recorder got it (default): the feed's own delay
//                                      is kept, the network leg is replaced by --md-latency-ms.
//                                      Needs a fit cache cut on receive time (beta-<b>-recv).
//                            exchange  exchange timestamp + --md-latency-ms (a feed with no
//                                      delay of its own; a best case)
//   --recorded-latency-ms X  the network latency inside the recorded receive times
//                          (received mode; default: lowest probe round trip / 2)
//   --fits PATH            SABR fit cache (plain JSONL; use <(gunzip -c ...))
//   --tob-out PATH         exchange-side top of book after every book message (CSV)
//   --checkpoint-dir DIR   write hourly book checkpoints (e.g. cache/books)
//   --config PATH          run config: accounts (strategy + hedger) and simulation settings
//                          (see run_config.hpp, config/run_m4.json). Needs --run-dir.
//   --run-dir DIR          where config.json and each account's fills.csv / pnl.csv /
//                          summary.json go (e.g. runs/<run_id>)
//   --fees PATH            fee schedule (default config/fees.json)
// Stats go to stderr.
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <algorithm>
#include <ctime>
#include <exception>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "checkpoint.hpp"
#include "event_loop.hpp"
#include "exchange_sim.hpp"
#include "fees.hpp"
#include "hedge_runner.hpp"
#include "hedger.hpp"
#include "input.hpp"
#include "parse.hpp"
#include "quoter.hpp"
#include "run_config.hpp"
#include "run_output.hpp"
#include "strategy.hpp"
#include "tob_dump.hpp"

namespace {

using namespace mm;

#ifdef NDEBUG
constexpr const char* kBuild = "optimized";
#else
constexpr const char* kBuild = "DEBUG (asserts on, not for timing)";
#endif

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "mm_engine: %s\n", msg.c_str());
    std::exit(2);
}

struct ProbeMin {
    double min_rtt_ms = std::numeric_limits<double>::infinity();
    std::int64_t probes = 0;
};

// Lowest recorded probe round trip across all recorder logs.
ProbeMin read_probes(const std::filesystem::path& log_dir) {
    ProbeMin p;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(log_dir, ec)) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with("recorder-") || entry.path().extension() != ".jsonl") continue;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\"event\": \"probe\"") == std::string::npos) continue;
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded() || !j.contains("rtt_ms")) continue;
            p.min_rtt_ms = std::min(p.min_rtt_ms, j["rtt_ms"].get<double>());
            ++p.probes;
        }
    }
    return p;
}

// Latency in ms: a finite number in [0, 60000]. A negative value would deliver events or orders
// before their input time (look-ahead); a huge one would overflow time + latency.
Nanos ms_arg(const char* flag, const char* v) {
    char* end = nullptr;
    double ms = std::strtod(v, &end);
    if (end == v || *end != '\0' || !std::isfinite(ms) || ms < 0 || ms * kMs > kMaxLatency) {
        die(std::string(flag) + " must be a number from 0 to 60000, got '" + v + "'");
    }
    return static_cast<Nanos>(std::llround(ms * kMs));
}

using oj = nlohmann::ordered_json;

// UTC, to the second. For reading; time_ns fields carry the exact value.
std::string utc(Nanos t) {
    if (t <= 0) return "";
    std::time_t secs = static_cast<std::time_t>(t / kSec);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

void print_late(const char* label, std::int64_t n, const LateHist& h, Nanos max_late) {
    std::fprintf(stderr, "%s %" PRId64 " behind the clock by (ms):", label, n);
    for (size_t i = 0; i < h.size(); ++i) {
        if (i < kLateEdgesMs.size()) std::fprintf(stderr, " <=%d: %" PRId64, kLateEdgesMs[i], h[i]);
        else std::fprintf(stderr, " more: %" PRId64, h[i]);
    }
    std::fprintf(stderr, "; max %.1f ms\n", static_cast<double>(max_late) / 1e6);
}

// A simulation run: the accounts, their quoters, and the output folder.
struct Run {
    RunConfig cfg;
    FeeSchedule fees;
    std::unique_ptr<RunOutput> out;
    std::unique_ptr<ExchangeSim> sim;
    std::vector<std::unique_ptr<Quoter>> quoters;
    std::vector<std::unique_ptr<HedgeRunner>> hedgers;  // one per account; null for "none"
    std::vector<oj> hedger_params;                      // as in use, defaults included
    oj shared;  // conditions every account of the run has in common
};

std::unique_ptr<Run> start_run(EventLoop& loop, const char* config_path, const char* run_dir,
                               const char* fees_path, const std::string& latency_source) {
    register_builtin_strategies();
    register_builtin_hedgers();
    auto run = std::make_unique<Run>();
    run->cfg = RunConfig::load(config_path);
    run->fees = FeeSchedule::load(fees_path);

    std::vector<std::string> names;
    for (const auto& a : run->cfg.accounts) names.push_back(a.name);
    run->out = std::make_unique<RunOutput>(run_dir, names);
    run->sim = std::make_unique<ExchangeSim>(loop, run->fees, run->cfg.sim, names, run->out.get());
    loop.add_exchange_observer(run->sim.get());

    oj accounts = oj::array();
    for (size_t i = 0; i < run->cfg.accounts.size(); ++i) {
        const AccountConfig& a = run->cfg.accounts[i];
        auto q = std::make_unique<Quoter>(loop, static_cast<std::uint32_t>(i),
                                          make_strategy(a.strategy, a.params), a.quoter, &run->fees);
        loop.add_market_observer(q.get());
        loop.add_order_observer(q.get());
        // Every hedger is built (so its parameters are checked and recorded); "none" gets no
        // runner because it never trades.
        auto hedger = make_hedger(a.hedger, a.hedger_params);
        run->hedger_params.push_back(hedger->params());
        std::unique_ptr<HedgeRunner> h;
        if (a.hedger != "none") {
            h = std::make_unique<HedgeRunner>(loop, static_cast<std::uint32_t>(i), std::move(hedger),
                                              run->fees, run->cfg.hedge);
            loop.add_market_observer(h.get());
            loop.add_order_observer(h.get());
        }
        accounts.push_back({{"name", a.name},
                            {"strategy", a.strategy},
                            {"params", q->strategy().params()},
                            {"fair_value_source", q->strategy().fair_value_source()},
                            {"hedger", a.hedger},
                            {"hedger_params", run->hedger_params.back()},
                            {"quoter", to_json(a.quoter)}});
        run->quoters.push_back(std::move(q));
        run->hedgers.push_back(std::move(h));
    }

    run->shared = {
        {"inputs", run->cfg.inputs},
        {"md_latency_ms", static_cast<double>(loop.config().md_latency) / 1e6},
        {"order_latency_ms", static_cast<double>(loop.config().order_latency) / 1e6},
        {"latency_source", latency_source},
        {"md_timing", loop.config().md_timing == MdTiming::AsReceived ? "received" : "exchange"},
        {"recorded_latency_ms", loop.config().md_timing == MdTiming::AsReceived
                                    ? oj(static_cast<double>(loop.config().recorded_latency) / 1e6)
                                    : oj(nullptr)},
        {"fee_schedule_date", run->fees.date_checked},
        {"fee_tier", run->fees.tier},
        {"trade_from_utc", run->cfg.trade_from_utc.empty() ? oj(nullptr) : oj(run->cfg.trade_from_utc)},
        {"trade_until_utc", run->cfg.trade_until_utc.empty() ? oj(nullptr) : oj(run->cfg.trade_until_utc)},
        {"hedge_interval_ms", static_cast<double>(run->cfg.hedge.interval) / 1e6},
        {"hedge_slippage", run->cfg.hedge.slippage},
    };
    oj config = run->shared;
    config.update(to_json(run->cfg.sim));
    config["fees_file"] = fees_path;
    config["fees"] = oj::parse(run->fees.raw_json);
    config["accounts"] = std::move(accounts);
    run->out->write_config(config);
    return run;
}

void finish_run(Run& run, const EventLoop& loop, const Instruments& reg) {
    run.sim->finish();
    // beta of the SABR fits the run was fed (null if none).
    oj beta = nullptr;
    if (!loop.view().fits().empty()) beta = loop.view().fits().begin()->second.beta;

    for (size_t i = 0; i < run.cfg.accounts.size(); ++i) {
        const AccountConfig& a = run.cfg.accounts[i];
        const Quoter& q = *run.quoters[i];
        const Account& acc = run.sim->account(i);
        run.out->write_fills(i, acc, reg);

        std::vector<std::string> quoted;
        for (InstrumentId id : q.stats().quoted) quoted.push_back(reg.name(id));
        std::sort(quoted.begin(), quoted.end());

        oj cond = run.shared;
        cond.update(to_json(run.cfg.sim));
        cond["strategy"] = a.strategy;
        cond["params"] = q.strategy().params();
        cond["fair_value_source"] = q.strategy().fair_value_source();
        cond["sabr_beta"] = beta;
        cond["hedger"] = a.hedger;
        cond["hedger_params"] = run.hedger_params[i];
        cond["quoter"] = to_json(a.quoter);
        cond["first_time_ns"] = run.out->first_time(i);
        cond["last_time_ns"] = run.out->last_time(i);
        cond["first_time_utc"] = utc(run.out->first_time(i));
        cond["last_time_utc"] = utc(run.out->last_time(i));
        cond["instruments_quoted"] = quoted.size();
        cond["instrument_names"] = quoted;
        // Expiries settled at Deribit's final delivery_price vs at the last estimate before
        // expiry (when the final ticker never arrived). Counted over all accounts of the run.
        cond["expiries_settled"] = {{"deribit_delivery_price", run.sim->settled_final()},
                                    {"estimate_fallback", run.sim->settled_estimate()}};

        oj summary = {{"account", a.name}, {"conditions", cond}};
        summary.update(account_result(acc, run.sim->stats(i), run.out->last_index(i)));
        summary["quoter"] = {{"evaluations", q.stats().evaluations},
                             {"places", q.stats().places},
                             {"replaces", q.stats().replaces},
                             {"cancels", q.stats().cancels}};
        if (const HedgeRunner* h = run.hedgers[i].get()) {
            summary["hedging"] = {{"checks", h->stats().checks},
                                  {"orders", h->stats().orders},
                                  {"fills", h->stats().fills},
                                  {"orders_not_fully_filled", h->stats().unfilled},
                                  {"orders_rejected", h->stats().rejected}};
            // Only when it happened: it should not, and the key's absence keeps summaries of
            // normal runs unchanged.
            if (h->stats().timed_out > 0) summary["hedging"]["orders_timed_out"] = h->stats().timed_out;
        }
        run.out->write_summary(i, summary);

        const PnlParts& p = acc.pnl();
        std::fprintf(stderr,
                     "account %s: pnl %.8f BTC (spread %.8f, fees %.8f, adverse %.8f, option mtm %.8f, "
                     "hedge %.8f), fills %" PRId64 " maker / %" PRId64 " taker, %zu options quoted\n",
                     a.name.c_str(), p.total(), p.spread, p.fees, p.adverse, p.option_mtm, p.hedge,
                     run.sim->stats(i).maker_fills, run.sim->stats(i).taker_fills, quoted.size());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<Nanos> md_lat, ord_lat, recorded_lat;
    MdTiming timing = MdTiming::AsReceived;
    const char* fits_path = nullptr;
    const char* tob_path = nullptr;
    const char* ckpt_dir = nullptr;
    const char* config_path = nullptr;
    const char* run_dir = nullptr;
    const char* fees_path = "config/fees.json";
    for (int i = 1; i < argc; ++i) {
        auto need = [&](const char* flag) {
            if (i + 1 >= argc) die(std::string(flag) + " needs a value");
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--md-latency-ms")) md_lat = ms_arg(argv[i], need(argv[i]));
        else if (!std::strcmp(argv[i], "--order-latency-ms")) ord_lat = ms_arg(argv[i], need(argv[i]));
        else if (!std::strcmp(argv[i], "--recorded-latency-ms")) recorded_lat = ms_arg(argv[i], need(argv[i]));
        else if (!std::strcmp(argv[i], "--md-timing")) {
            const char* v = need(argv[i]);
            if (!std::strcmp(v, "received")) timing = MdTiming::AsReceived;
            else if (!std::strcmp(v, "exchange")) timing = MdTiming::ExchangeTime;
            else die(std::string("--md-timing must be received or exchange, got '") + v + "'");
        }
        else if (!std::strcmp(argv[i], "--fits")) fits_path = need(argv[i]);
        else if (!std::strcmp(argv[i], "--tob-out")) tob_path = need(argv[i]);
        else if (!std::strcmp(argv[i], "--checkpoint-dir")) ckpt_dir = need(argv[i]);
        else if (!std::strcmp(argv[i], "--config")) config_path = need(argv[i]);
        else if (!std::strcmp(argv[i], "--run-dir")) run_dir = need(argv[i]);
        else if (!std::strcmp(argv[i], "--fees")) fees_path = need(argv[i]);
        else die(std::string("unknown option ") + argv[i]);
    }

    if ((config_path == nullptr) != (run_dir == nullptr)) die("--config and --run-dir go together");

    // The timing mode only matters to strategies; without a run nothing reads the view.
    if (config_path == nullptr) timing = MdTiming::ExchangeTime;
    const bool need_recorded = timing == MdTiming::AsReceived && !recorded_lat;

    std::string latency_source = "flags";
    if (!md_lat || !ord_lat || need_recorded) {
        const char* data_dir = std::getenv("DATA_DIR");
        auto log_dir = std::filesystem::path(data_dir ? data_dir : "data") / "logs";
        ProbeMin p = read_probes(log_dir);
        if (p.probes == 0) {
            die("no latency probes in " + log_dir.string() +
                "; pass --md-latency-ms and --order-latency-ms" +
                (need_recorded ? " and --recorded-latency-ms" : ""));
        }
        Nanos one_way = static_cast<Nanos>(std::llround(p.min_rtt_ms * kMs / 2));
        if (need_recorded) recorded_lat = one_way;
        if (!md_lat || !ord_lat) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "lowest probe RTT %.3f ms / 2 over %" PRId64 " probes in %s",
                          p.min_rtt_ms, p.probes, log_dir.c_str());
            latency_source = buf;
        }
        if (!md_lat) md_lat = one_way;
        if (!ord_lat) ord_lat = one_way;
    }
    std::fprintf(stderr, "latency: md %.3f ms, order %.3f ms (%s)\n", *md_lat / 1e6, *ord_lat / 1e6,
                 latency_source.c_str());
    if (timing == MdTiming::AsReceived) {
        std::fprintf(stderr, "market data timing: as received (recorded latency %.3f ms replaced by md latency)\n",
                     *recorded_lat / 1e6);
    } else if (config_path) {
        std::fprintf(stderr, "market data timing: exchange timestamp + md latency (no feed delay)\n");
    }

    Instruments reg;
    Parser parser(reg);
    LineReader main_in(stdin);
    std::unique_ptr<LineReader> fits_in;
    std::FILE* fits_file = nullptr;
    if (fits_path) {
        fits_file = std::fopen(fits_path, "r");
        if (!fits_file) die(std::string("cannot open ") + fits_path);
        fits_in = std::make_unique<LineReader>(fits_file);
    }
    MergedInput input(main_in, fits_in.get(), parser);

    EventLoop loop({*md_lat, *ord_lat, timing, timing == MdTiming::AsReceived ? *recorded_lat : 0});
    std::FILE* tob_file = nullptr;
    std::unique_ptr<TobDump> tob;
    if (tob_path) {
        tob_file = std::fopen(tob_path, "w");
        if (!tob_file) die(std::string("cannot write ") + tob_path);
        std::setvbuf(tob_file, nullptr, _IOFBF, 1 << 20);
        tob = std::make_unique<TobDump>(tob_file, reg);
        loop.add_exchange_observer(tob.get());
    }
    std::unique_ptr<CheckpointWriter> ckpt;
    if (ckpt_dir) ckpt = std::make_unique<CheckpointWriter>(ckpt_dir, reg, parser);
    std::unique_ptr<Run> run;
    try {
        if (config_path) run = start_run(loop, config_path, run_dir, fees_path, latency_source);
    } catch (const std::exception& ex) {
        die(ex.what());
    }

    auto t0 = std::chrono::steady_clock::now();
    try {
        Event e;
        while (input.next(e)) {
            // Fits stamped after the last market event would only push the clock (and with it
            // expiries and pnl rows) past the end of the data.
            if (run && input.main_exhausted()) break;
            if (run && run->cfg.trade_until != 0 && loop.now() >= run->cfg.trade_until + kRunOff) break;
            if (run && timing == MdTiming::AsReceived) {
                const auto* fit = std::get_if<SabrFit>(&e.payload);
                if (fit != nullptr && !fit->recv_clock) {
                    die("--md-timing received needs fits cut on receive time (sabr_sidecar/fit.py "
                        "--clock recv, cache/sabr/beta-<b>-recv); this cache was cut on exchange time");
                }
            }
            // Raw files rotate on recv_ns, so checkpoints must too (fits have no recv_ns).
            if (ckpt && e.recv_ns) ckpt->before(e.recv_ns, loop.exchange());
            loop.process(std::move(e));
        }
        loop.finish();
        if (run) finish_run(*run, loop, reg);
    } catch (const std::exception& ex) {
        die(ex.what());
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (tob_file) std::fclose(tob_file);
    if (fits_file) std::fclose(fits_file);

    const auto& s = loop.stats();
    std::fprintf(stderr,
                 "lines %" PRId64 ", malformed %" PRId64 ", ignored %" PRId64 ", events %" PRId64 "\n"
                 "books %" PRId64 " (snapshots %" PRId64 ", gaps %" PRId64 ", no_snapshot %" PRId64 ")"
                 ", trades %" PRId64 ", tickers %" PRId64 ", index %" PRId64 ", fits %" PRId64
                 ", defs %" PRId64 ", recorder %" PRId64 "\n"
                 "late events %" PRId64 ", fits out of order %" PRId64 ", checkpoints %d\n"
                 "wall %.2f s (%.0f events/s), %s build\n",
                 main_in.lines, parser.malformed, parser.ignored, s.events, s.books, s.snapshots,
                 s.gaps, s.no_snapshot, s.trades, s.tickers, s.index, s.fits, s.defs, s.recorder,
                 s.late_events, input.fits_out_of_order, ckpt ? ckpt->written : 0, secs,
                 secs > 0 ? s.events / secs : 0.0, kBuild);
    print_late("late events", s.late_events, s.late_hist, s.max_late);
    print_late("late trades", s.late_trades, s.late_trade_hist, s.max_late_trade);
    return 0;
}
