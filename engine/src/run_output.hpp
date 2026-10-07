// Run folder writer (SPEC 4):
//   <run_dir>/config.json
//   <run_dir>/<account>/pnl.csv       time series, written as the run goes
//   <run_dir>/<account>/fills.csv     one row per fill, written at the end (markouts need the
//                                     60 s after each fill)
//   <run_dir>/<account>/summary.json
// Nothing here depends on wall time, so the same input and config give identical files.
//
// Units: BTC unless a column says otherwise. Prices are as quoted on Deribit (options in BTC,
// the perpetual in USD). edge and markouts are BTC per unit of amount (per contract for
// options, per USD for the perpetual), positive = in our favour:
//   edge_btc     mid at the fill vs the fill price
//   markout_*    mid 1 s / 10 s / 60 s after the fill vs the fill price (empty if the run ended
//                first)
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "exchange_sim.hpp"
#include "instruments.hpp"

namespace mm {

class RunOutput final : public RunSink {
public:
    // Creates the folders and opens every pnl.csv. Throws std::runtime_error on failure.
    RunOutput(std::filesystem::path dir, const std::vector<std::string>& accounts);
    ~RunOutput() override;
    RunOutput(const RunOutput&) = delete;
    RunOutput& operator=(const RunOutput&) = delete;

    void pnl_row(size_t account, const PnlRow& row) override;
    void write_config(const nlohmann::ordered_json& j) const;
    void write_fills(size_t account, const Account& acc, const Instruments& reg) const;
    void write_summary(size_t account, const nlohmann::ordered_json& j) const;

    // Time and index of the first and last pnl row of an account (0 / NaN before any row).
    Nanos first_time(size_t account) const { return spans_.at(account).first; }
    Nanos last_time(size_t account) const { return spans_.at(account).last; }
    double last_index(size_t account) const { return spans_.at(account).index; }

private:
    struct Span {
        Nanos first = 0, last = 0;
        double index = kNaN;
    };
    std::filesystem::path dir_;
    std::vector<std::string> names_;
    std::vector<std::FILE*> pnl_;
    std::vector<Span> spans_;
};

// The numbers of one account for summary.json: result, activity, markouts. The caller adds the
// run's conditions around it.
nlohmann::ordered_json account_result(const Account& acc, const AccountStats& stats, double last_index);

}  // namespace mm
