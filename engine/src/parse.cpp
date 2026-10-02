#include "parse.hpp"

#include <nlohmann/json.hpp>

namespace mm {

using json = nlohmann::json;

namespace {

double num(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<double>() : kNaN;
}

Nanos ms_to_ns(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<Nanos>() * kMs : 0;
}

void read_entries(const json& arr, std::vector<BookEntry>& out) {
    out.reserve(arr.size());
    for (const auto& e : arr) {
        const auto& a = e[0].get_ref<const std::string&>();
        BookAction act = a == "new" ? BookAction::New
                         : a == "delete" ? BookAction::Delete
                                         : BookAction::Change;
        out.push_back({act, to_e8(e[1].get<double>()), e[2].get<double>()});
    }
}

InstrumentDef read_def(const json& r, Instruments& reg) {
    InstrumentDef d;
    d.name = r.at("instrument_name").get<std::string>();
    d.id = reg.id(d.name);
    auto kind = r.value("kind", std::string());
    d.kind = kind == "option" ? InstrumentKind::Option
             : r.value("settlement_period", std::string()) == "perpetual" ? InstrumentKind::Perpetual
                                                                           : InstrumentKind::Other;
    d.is_call = r.value("option_type", std::string()) == "call";
    d.strike = num(r, "strike");
    d.expiry = ms_to_ns(r, "expiration_timestamp");
    d.tick_size = num(r, "tick_size");
    if (auto it = r.find("tick_size_steps"); it != r.end() && it->is_array()) {
        for (const auto& s : *it) d.tick_steps.emplace_back(num(s, "above_price"), num(s, "tick_size"));
    }
    d.contract_size = num(r, "contract_size");
    d.min_trade_amount = num(r, "min_trade_amount");
    d.settlement_period = r.value("settlement_period", std::string());
    d.maker_fee = num(r, "maker_commission");
    d.taker_fee = num(r, "taker_commission");
    return d;
}

// The "msg" object's original text. Recorder lines are {"recv_ns":N,"msg":<raw>}, so it is
// everything after the first "msg": up to the final brace; otherwise re-serialize.
std::string raw_msg(std::string_view line, const json& msg) {
    auto i = line.find("\"msg\":");
    if (i != std::string_view::npos && line.size() > i + 7 && line.back() == '}') {
        return std::string(line.substr(i + 6, line.size() - i - 7));
    }
    return msg.dump();
}

RecorderEvent::Kind recorder_kind(const std::string& t) {
    using K = RecorderEvent::Kind;
    if (t == "connect") return K::Connect;
    if (t == "disconnect") return K::Disconnect;
    if (t == "gap") return K::Gap;
    if (t == "resubscribe") return K::Resubscribe;
    if (t == "universe") return K::Universe;
    return K::Other;
}

}  // namespace

void Parser::parse_line(std::string_view line, std::vector<Event>& out) {
    json rec = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (rec.is_discarded() || !rec.is_object()) {
        ++malformed;
        return;
    }
    try {
        Nanos recv_ns = rec.at("recv_ns").get<Nanos>();

        if (auto ev = rec.find("event"); ev != rec.end()) {
            out.push_back({0, recv_ns, RecorderEvent{recorder_kind(ev->value("type", std::string()))}});
            return;
        }

        const json& msg = rec.at("msg");
        if (auto res = msg.find("result"); res != msg.end()) {
            // Instrument lists are the only RPC replies the recorder stores.
            Nanos t = msg.contains("usOut") ? msg["usOut"].get<Nanos>() * kUs : 0;
            if (res->is_array() && !res->empty() && (*res)[0].contains("instrument_name")) {
                for (const auto& r : *res) out.push_back({t, recv_ns, read_def(r, reg_)});
                last_options_msg = raw_msg(line, msg);
            } else if (res->is_object() && res->contains("instrument_name")) {
                out.push_back({t, recv_ns, read_def(*res, reg_)});
                last_perp_msg = raw_msg(line, msg);
            } else {
                ++ignored;
            }
            return;
        }

        if (msg.value("method", std::string()) != "subscription") {
            ++ignored;
            return;
        }
        const json& params = msg.at("params");
        const auto& channel = params.at("channel").get_ref<const std::string&>();
        const json& data = params.at("data");

        if (channel.starts_with("book.")) {
            BookUpdate u;
            u.id = reg_.id(data.at("instrument_name").get_ref<const std::string&>());
            u.snapshot = data.value("type", std::string()) == "snapshot";
            u.change_id = data.at("change_id").get<std::int64_t>();
            if (auto p = data.find("prev_change_id"); p != data.end()) u.prev_change_id = p->get<std::int64_t>();
            read_entries(data.at("bids"), u.bids);
            read_entries(data.at("asks"), u.asks);
            out.push_back({ms_to_ns(data, "timestamp"), recv_ns, std::move(u)});
        } else if (channel.starts_with("trades.")) {
            for (const auto& d : data) {
                Trade t;
                t.id = reg_.id(d.at("instrument_name").get_ref<const std::string&>());
                t.price = to_e8(d.at("price").get<double>());
                t.amount = d.at("amount").get<double>();
                t.buy = d.value("direction", std::string()) == "buy";
                t.trade_seq = d.value("trade_seq", std::int64_t{0});
                t.iv = num(d, "iv");
                t.mark_price = num(d, "mark_price");
                t.index_price = num(d, "index_price");
                out.push_back({ms_to_ns(d, "timestamp"), recv_ns, t});
            }
        } else if (channel.starts_with("ticker.")) {
            Ticker k;
            k.id = reg_.id(data.at("instrument_name").get_ref<const std::string&>());
            k.mark_price = num(data, "mark_price");
            k.mark_iv = num(data, "mark_iv");
            k.bid_iv = num(data, "bid_iv");
            k.ask_iv = num(data, "ask_iv");
            k.underlying_price = num(data, "underlying_price");
            k.index_price = num(data, "index_price");
            k.best_bid = num(data, "best_bid_price");
            k.best_ask = num(data, "best_ask_price");
            k.best_bid_amount = num(data, "best_bid_amount");
            k.best_ask_amount = num(data, "best_ask_amount");
            if (auto g = data.find("greeks"); g != data.end()) {
                k.delta = num(*g, "delta");
                k.gamma = num(*g, "gamma");
                k.vega = num(*g, "vega");
                k.theta = num(*g, "theta");
            }
            out.push_back({ms_to_ns(data, "timestamp"), recv_ns, k});
        } else if (channel.starts_with("deribit_price_index.")) {
            out.push_back({ms_to_ns(data, "timestamp"), recv_ns, IndexPrice{num(data, "price")}});
        } else {
            ++ignored;
        }
    } catch (const json::exception&) {
        ++malformed;
    }
}

bool Parser::parse_fit_line(std::string_view line, Event& out) {
    json j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        ++malformed;
        return false;
    }
    try {
        SabrFit f;
        f.fit_time = j.at("fit_time_ns").get<Nanos>();
        f.compute_ns = j.at("compute_ns").get<Nanos>();
        f.expiry = j.at("expiry_ms").get<Nanos>() * kMs;
        f.beta = num(j, "beta");
        f.alpha = num(j, "alpha");
        f.rho = num(j, "rho");
        f.nu = num(j, "nu");
        f.forward = num(j, "forward");
        f.rmse_vol = num(j, "rmse_vol");
        out = {f.fit_time + f.compute_ns, 0, f};
        return true;
    } catch (const json::exception&) {
        ++malformed;
        return false;
    }
}

}  // namespace mm
