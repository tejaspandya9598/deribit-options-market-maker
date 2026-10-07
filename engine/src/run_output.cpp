#include "run_output.hpp"

#include <cinttypes>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace mm {

namespace {

void write_json(const std::filesystem::path& path, const nlohmann::ordered_json& j) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << j.dump(2) << '\n';
}

// Empty field for NaN, so a missing markout cannot be read as zero.
void field(std::FILE* f, double v, const char* fmt) {
    std::fputc(',', f);
    if (std::isfinite(v)) std::fprintf(f, fmt, v + 0.0);  // + 0.0: never print "-0"
}

}  // namespace

RunOutput::RunOutput(std::filesystem::path dir, const std::vector<std::string>& accounts)
    : dir_(std::move(dir)), names_(accounts), spans_(accounts.size()) {
    for (const auto& name : names_) {
        std::filesystem::create_directories(dir_ / name);
        const auto path = dir_ / name / "pnl.csv";
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (f == nullptr) throw std::runtime_error("cannot write " + path.string());
        std::setvbuf(f, nullptr, _IOFBF, 1 << 16);
        std::fputs("time_ns,equity_btc,cash_btc,pnl_btc,spread_btc,fees_btc,adverse_btc,option_mtm_btc,"
                   "hedge_btc,delta_btc,gamma,vega_usd_per_vp,positions,index_usd\n", f);
        pnl_.push_back(f);
    }
}

RunOutput::~RunOutput() {
    for (std::FILE* f : pnl_) std::fclose(f);
}

void RunOutput::pnl_row(size_t account, const PnlRow& r) {
    Span& s = spans_.at(account);
    if (s.first == 0) s.first = r.time;
    s.last = r.time;
    s.index = r.index;
    std::FILE* f = pnl_.at(account);
    std::fprintf(f, "%" PRId64 ",%.10f,%.10f,%.10f,%.10f,%.10f,%.10f,%.10f,%.10f,%.8f,%.10f,%.6f,%d",
                 r.time, r.equity, r.cash, r.pnl.total(), r.pnl.spread, r.pnl.fees, r.pnl.adverse,
                 r.pnl.option_mtm, r.pnl.hedge, r.delta, r.gamma, r.vega, r.positions);
    field(f, r.index, "%.2f");
    std::fputc('\n', f);
}

void RunOutput::write_config(const nlohmann::ordered_json& j) const { write_json(dir_ / "config.json", j); }

void RunOutput::write_summary(size_t account, const nlohmann::ordered_json& j) const {
    write_json(dir_ / names_.at(account) / "summary.json", j);
}

void RunOutput::write_fills(size_t account, const Account& acc, const Instruments& reg) const {
    const auto path = dir_ / names_.at(account) / "fills.csv";
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) throw std::runtime_error("cannot write " + path.string());
    std::fputs("time_ns,instrument,client_id,side,liquidity,price,amount,fee_btc,mid,edge_btc,"
               "markout_1s_btc,markout_10s_btc,markout_60s_btc,position_after\n", f);
    for (const FillRecord& x : acc.fills()) {
        std::fprintf(f, "%" PRId64 ",%s,%" PRIu64 ",%s,%s,%.10g,%.10g,%.10f", x.time,
                     reg.name(x.instrument).c_str(), x.client_id, x.side == Side::Buy ? "buy" : "sell",
                     x.maker ? "maker" : "taker", x.price, x.amount, x.fee);
        field(f, x.mid, "%.10g");
        field(f, sign(x.side) * (x.u_mid - x.u_fill), "%.12g");
        for (double m : x.markout) field(f, m, "%.12g");
        std::fprintf(f, ",%.10g\n", x.position_after);
    }
    std::fclose(f);
}

nlohmann::ordered_json account_result(const Account& acc, const AccountStats& stats, double last_index) {
    using oj = nlohmann::ordered_json;
    const PnlParts& p = acc.pnl();
    const double equity = acc.equity();

    // Amount-weighted averages over option maker fills, per contract.
    double option_volume = 0, perp_volume = 0, edge = 0;
    std::array<double, kMarkoutHorizons.size()> sum{}, weight{};
    for (const FillRecord& f : acc.fills()) {
        if (!f.option) {
            perp_volume += f.amount;
            continue;
        }
        option_volume += f.amount;
        if (!f.maker) continue;
        edge += f.amount * sign(f.side) * (f.u_mid - f.u_fill);
        for (size_t h = 0; h < sum.size(); ++h) {
            if (!std::isfinite(f.markout[h])) continue;
            sum[h] += f.amount * f.markout[h];
            weight[h] += f.amount;
        }
    }
    auto avg = [](double s, double w) { return w > 0 ? oj(s / w) : oj(nullptr); };
    double maker_volume = 0;
    for (const FillRecord& f : acc.fills()) {
        if (f.option && f.maker) maker_volume += f.amount;
    }

    oj pnl = {
        {"total", p.total()},
        {"spread", p.spread},
        {"fees", p.fees},
        {"adverse_selection_60s", p.adverse},
        {"option_mtm", p.option_mtm},
        {"hedge", p.hedge},
    };
    oj out = {
        {"starting_equity_btc", acc.starting_equity()},
        {"final_equity_btc", equity},
        {"pnl_btc", pnl},
        // Should be ~0: equity is cash + marked positions, the parts are accumulated separately.
        {"accounting_residual_btc", equity - acc.starting_equity() - p.total()},
        {"last_index_usd", std::isfinite(last_index) ? oj(last_index) : oj(nullptr)},
        {"pnl_usd_at_last_index", std::isfinite(last_index) ? oj(p.total() * last_index) : oj(nullptr)},
        {"orders",
         {{"placed", stats.placed},
          {"replaced", stats.replaced},
          {"cancelled", stats.cancelled},
          {"rejected_would_cross", stats.rejected_cross},
          {"rejected_other", stats.rejected_other}}},
        {"fills",
         {{"maker", stats.maker_fills},
          {"taker", stats.taker_fills},
          {"option_volume_contracts", option_volume},
          {"perp_volume_usd", perp_volume}}},
        {"settlements", {{"positions_settled", stats.settlements}, {"delivery_fees_btc", stats.delivery_fees}}},
        {"markouts_btc_per_contract",
         {{"note", "option maker fills, amount-weighted; mid after the fill vs fill price, positive = in our favour"},
          {"edge_at_fill", avg(edge, maker_volume)},
          {"after_1s", avg(sum[0], weight[0])},
          {"after_10s", avg(sum[1], weight[1])},
          {"after_60s", avg(sum[2], weight[2])}}},
    };
    return out;
}

}  // namespace mm
