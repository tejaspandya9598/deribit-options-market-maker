#include <doctest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "event_loop.hpp"
#include "exchange_sim.hpp"
#include "instruments.hpp"
#include "parse.hpp"
#include "state_out.hpp"

namespace {

using namespace mm;
using json = nlohmann::json;

struct ViewProbe final : MarketObserver {
    struct BookSeen { Nanos stamp; Nanos now; std::int64_t change_id; bool valid; };
    struct TickerSeen { Nanos stamp; Nanos now; double mark; };
    std::vector<BookSeen> books;
    std::vector<TickerSeen> tickers;

    void on_market(const Event& e, const MarketState& view, Nanos now) override {
        if (const auto* b = std::get_if<BookUpdate>(&e.payload)) {
            const auto* book = view.book(b->id);
            books.push_back({e.time, now, book ? book->change_id() : -1,
                             book != nullptr && book->valid()});
        } else if (const auto* t = std::get_if<Ticker>(&e.payload)) {
            const auto* ticker = view.ticker(t->id);
            tickers.push_back({e.time, now, ticker ? ticker->mark_price : kNaN});
        }
    }
};

struct FitProbe final : MarketObserver {
    std::vector<std::pair<Nanos, size_t>> fits_at_delivery;
    void on_market(const Event&, const MarketState& view, Nanos now) override {
        fits_at_delivery.emplace_back(now, view.fits().size());
    }
};

struct Rows final : RunSink {
    std::vector<PnlRow> rows;
    void pnl_row(size_t, const PnlRow& row) override { rows.push_back(row); }
};

struct StateRun {
    std::vector<PnlRow> rows;
    std::vector<FillRecord> fills;
    std::string stream;
};

StateRun run_state_scenario(bool state_enabled) {
    EventLoop loop({0, 0});
    Instruments reg;
    const InstrumentId option = reg.id("BTC-TEST-100-C");
    const InstrumentId perp = reg.id("BTC-PERPETUAL");
    FeeSchedule fees;
    fees.option_maker = 0.0001;
    fees.option_cap = 0.125;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    cfg.pnl_interval = kSec;
    Rows rows;
    ExchangeSim sim(loop, fees, cfg, {"acct"}, &rows);
    loop.add_exchange_observer(&sim);

    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::tmpfile(), &std::fclose);
    REQUIRE(file != nullptr);
    std::unique_ptr<StateWriter> writer;
    if (state_enabled) {
        writer = std::make_unique<StateWriter>(file.get(), sim, reg, kSec);
        loop.add_exchange_observer(writer.get());
    }

    InstrumentDef d;
    d.id = option;
    d.name = reg.name(option);
    d.kind = InstrumentKind::Option;
    d.is_call = true;
    d.strike = 100000;
    d.expiry = 100 * kSec;
    d.tick_size = 0.0001;
    d.min_trade_amount = 0.1;
    loop.process({kSec, kSec, d});

    InstrumentDef pd;
    pd.id = perp;
    pd.name = reg.name(perp);
    pd.kind = InstrumentKind::Perpetual;
    loop.process({kSec + 1, kSec + 1, pd});

    BookUpdate book;
    book.id = option;
    book.snapshot = true;
    book.change_id = 1;
    book.bids = {{BookAction::New, to_e8(0.01), 1.0}};
    book.asks = {{BookAction::New, to_e8(0.02), 1.0}};
    loop.process({2 * kSec, 2 * kSec, book});

    Ticker ticker;
    ticker.id = option;
    ticker.mark_price = 0.015;
    ticker.underlying_price = 100000;
    ticker.index_price = kNaN;
    ticker.mark_iv = kNaN;
    loop.process({2 * kSec + 1, 2 * kSec + 1, ticker});

    OrderRequest bid;
    bid.account = 0;
    bid.client_id = 11;
    bid.action = OrderAction::Place;
    bid.type = OrderType::PostOnly;
    bid.instrument = option;
    bid.side = Side::Buy;
    bid.price = to_e8(0.009);
    bid.amount = 0.1;
    sim.on_order(bid, loop.exchange(), loop.now());

    OrderRequest ask = bid;
    ask.client_id = 12;
    ask.side = Side::Sell;
    ask.price = to_e8(0.021);
    sim.on_order(ask, loop.exchange(), loop.now());

    OrderRequest cancel = ask;
    cancel.action = OrderAction::Cancel;
    sim.on_order(cancel, loop.exchange(), loop.now());

    loop.process({3 * kSec, 3 * kSec, IndexPrice{kNaN}});
    Trade trade;
    trade.id = option;
    trade.price = to_e8(0.008);
    trade.amount = 0.1;
    trade.buy = false;
    loop.process({3 * kSec + 500 * kMs, 3 * kSec + 500 * kMs, trade});
    loop.process({4 * kSec, 4 * kSec, IndexPrice{kNaN}});
    loop.finish();
    sim.finish();
    if (writer) writer->finish(loop.exchange(), loop.now());

    std::string stream;
    if (writer) {
        std::rewind(file.get());
        char buf[4096];
        while (const size_t n = std::fread(buf, 1, sizeof(buf), file.get())) stream.append(buf, n);
    }
    return {std::move(rows.rows), sim.account(0).fills(), std::move(stream)};
}

}  // namespace

TEST_CASE("M6 view keeps decreasing-stamp books and tickers in arrival order") {
    for (const MdTiming timing : {MdTiming::ExchangeTime, MdTiming::AsReceived}) {
        for (const Nanos latency : {Nanos{0}, Nanos{50}}) {
            EventLoop loop({latency, 0, timing, 0});
            const InstrumentId id = 0;
            ViewProbe probe;
            loop.add_market_observer(&probe);

            BookUpdate first;
            first.id = id;
            first.snapshot = true;
            first.change_id = 1;
            first.bids = {{BookAction::New, 100, 1}};
            first.asks = {{BookAction::New, 110, 1}};
            loop.process({1000, 1000, first});
            loop.process({1100, 1100, Ticker{.id = id, .mark_price = 10}});

            BookUpdate second = first;
            second.snapshot = false;
            second.change_id = 2;
            second.prev_change_id = 1;
            second.bids = {{BookAction::Change, 100, 2}};
            loop.process({3000, 3000, second});
            loop.process({3100, 3100, Ticker{.id = id, .mark_price = 11}});

            BookUpdate late = second;
            late.change_id = 3;
            late.prev_change_id = 2;
            late.bids = {{BookAction::Change, 100, 3}};
            loop.process({2500, 4000, late});
            loop.process({2600, 4100, Ticker{.id = id, .mark_price = 12}});
            loop.finish();

            REQUIRE(probe.books.size() == 3);
            CHECK(probe.books[0].change_id == 1);
            CHECK(probe.books[1].change_id == 2);
            CHECK(probe.books[2].change_id == 3);
            CHECK(probe.books[2].valid);
            REQUIRE(probe.tickers.size() == 3);
            CHECK(probe.tickers[0].mark == 10);
            CHECK(probe.tickers[1].mark == 11);
            CHECK(probe.tickers[2].mark == 12);
            for (const auto& seen : probe.books) CHECK(seen.now >= seen.stamp + latency);
            for (const auto& seen : probe.tickers) CHECK(seen.now >= seen.stamp + latency);
        }
    }
}

TEST_CASE("M6 fit lines in the main stream are parsed and unavailable before fit plus compute") {
    Instruments reg;
    Parser parser(reg);
    std::vector<Event> events;
    parser.parse_line(
        R"({"fit_time_ns":1000,"compute_ns":5,"expiry_ms":2000,"beta":0.5,"alpha":0.2,"rho":-0.1,"nu":0.4,"forward":100000,"rmse_vol":0.001,"clock":"exchange"})",
        events);
    REQUIRE(events.size() == 1);
    CHECK(events.front().time == 1005);
    CHECK(std::get<SabrFit>(events.front().payload).recv_clock == false);

    EventLoop loop({50, 0, MdTiming::ExchangeTime, 0});
    FitProbe probe;
    loop.add_market_observer(&probe);
    loop.process({1004, 1004, IndexPrice{1}});
    loop.process(events.front());
    CHECK(loop.view().fits().empty());
    loop.process({1005, 1005, IndexPrice{2}});
    REQUIRE(probe.fits_at_delivery.size() == 1);
    CHECK(probe.fits_at_delivery[0] == std::pair<Nanos, size_t>{1005, 1});
    loop.process({1054, 1054, IndexPrice{3}});
    REQUIRE(probe.fits_at_delivery.size() == 2);
    CHECK(probe.fits_at_delivery[1] == std::pair<Nanos, size_t>{1054, 1});
}

TEST_CASE("M6 state snapshots report only new fills and active quotes without changing run rows") {
    const StateRun plain = run_state_scenario(false);
    const StateRun streamed = run_state_scenario(true);
    const auto same_double = [](double lhs, double rhs) {
        return lhs == rhs || (std::isnan(lhs) && std::isnan(rhs));
    };
    REQUIRE(plain.fills.size() == 1);
    REQUIRE(streamed.fills.size() == 1);
    CHECK(streamed.fills[0].time == plain.fills[0].time);
    CHECK(streamed.fills[0].instrument == plain.fills[0].instrument);
    CHECK(streamed.fills[0].client_id == plain.fills[0].client_id);
    CHECK(streamed.fills[0].side == plain.fills[0].side);
    CHECK(streamed.fills[0].option == plain.fills[0].option);
    CHECK(streamed.fills[0].maker == plain.fills[0].maker);
    CHECK(streamed.fills[0].price == plain.fills[0].price);
    CHECK(streamed.fills[0].amount == plain.fills[0].amount);
    CHECK(streamed.fills[0].fee == plain.fills[0].fee);
    CHECK(streamed.fills[0].u_fill == plain.fills[0].u_fill);
    CHECK(streamed.fills[0].position_after == plain.fills[0].position_after);
    REQUIRE(streamed.rows.size() == plain.rows.size());
    for (size_t i = 0; i < plain.rows.size(); ++i) {
        CHECK(streamed.rows[i].time == plain.rows[i].time);
        CHECK(streamed.rows[i].equity == doctest::Approx(plain.rows[i].equity));
        CHECK(streamed.rows[i].cash == doctest::Approx(plain.rows[i].cash));
        CHECK(streamed.rows[i].pnl.total() == doctest::Approx(plain.rows[i].pnl.total()));
        CHECK(streamed.rows[i].pnl.spread == doctest::Approx(plain.rows[i].pnl.spread));
        CHECK(streamed.rows[i].pnl.fees == doctest::Approx(plain.rows[i].pnl.fees));
        CHECK(streamed.rows[i].pnl.adverse == doctest::Approx(plain.rows[i].pnl.adverse));
        CHECK(streamed.rows[i].pnl.option_mtm == doctest::Approx(plain.rows[i].pnl.option_mtm));
        CHECK(streamed.rows[i].pnl.hedge == doctest::Approx(plain.rows[i].pnl.hedge));
        CHECK(same_double(streamed.rows[i].delta, plain.rows[i].delta));
        CHECK(same_double(streamed.rows[i].gamma, plain.rows[i].gamma));
        CHECK(same_double(streamed.rows[i].vega, plain.rows[i].vega));
        CHECK(streamed.rows[i].positions == plain.rows[i].positions);
        CHECK(same_double(streamed.rows[i].index, plain.rows[i].index));
    }

    std::vector<json> lines;
    std::istringstream input(streamed.stream);
    std::string line;
    while (std::getline(input, line)) lines.push_back(json::parse(line));
    REQUIRE(lines.size() >= 3);
    CHECK(lines.back() == json{{"type", "end"}});
    CHECK(lines.front()["type"] == "state");
    CHECK(lines.front()["index"].is_null());
    CHECK(lines.front()["books"][0][3].is_null());  // non-finite mark IV is JSON null
    CHECK(lines.front()["accounts"][0]["quotes"].size() == 1);  // cancelled ask is omitted

    size_t fill_messages = 0;
    for (const auto& state : lines) {
        if (state.value("type", "") != "state") continue;
        fill_messages += state["fills"].size();
    }
    CHECK(fill_messages == 1);
    CHECK(lines[lines.size() - 2]["accounts"][0]["fills"] == 1);
    CHECK(lines[lines.size() - 2]["accounts"][0]["quotes"].empty());
}

TEST_CASE("peek matches snapshot for positions built from a recorded market checkpoint") {
    namespace fs = std::filesystem;
    fs::path root = fs::current_path();
    while (!root.empty() && !fs::exists(root / "cache/books/2026-09-29/00.jsonl")) {
        const fs::path parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    const fs::path checkpoint = root / "cache/books/2026-09-29/00.jsonl";
    REQUIRE(fs::exists(checkpoint));

    EventLoop loop({0, 0});
    Instruments reg;
    Parser parser(reg);
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    cfg.pnl_interval = kSec;
    cfg.settle_wait = 60 * kSec;
    ExchangeSim sim(loop, fees, cfg, {"recorded-sample"});
    loop.add_exchange_observer(&sim);

    std::vector<Event> events;
    {
        std::ifstream input(checkpoint);
        REQUIRE(input.good());
        std::string line;
        while (std::getline(input, line)) parser.parse_line(line, events);
    }
    REQUIRE(parser.malformed == 0);

    std::map<InstrumentId, InstrumentDef> definitions;
    std::map<InstrumentId, bool> has_two_sided_book;
    for (const Event& e : events) {
        if (const auto* d = std::get_if<InstrumentDef>(&e.payload)) {
            if (d->kind == InstrumentKind::Option && d->expiry > 0) definitions[d->id] = *d;
        } else if (const auto* b = std::get_if<BookUpdate>(&e.payload)) {
            has_two_sided_book[b->id] = !b->bids.empty() && !b->asks.empty();
        }
    }
    std::map<Nanos, std::vector<InstrumentId>> candidates;
    for (const auto& [id, d] : definitions) {
        if (has_two_sided_book[id]) candidates[d.expiry].push_back(id);
    }
    REQUIRE(candidates.size() >= 2);
    const auto first_expiry = candidates.begin();
    const auto second_expiry = std::next(first_expiry);
    REQUIRE(first_expiry->second.size() >= 1);
    REQUIRE(second_expiry->second.size() >= 2);
    const InstrumentId expired_id = first_expiry->second.front();
    const InstrumentId young_id = second_expiry->second.front();
    const InstrumentId no_market_mark_id = second_expiry->second[1];
    REQUIRE(expired_id >= 0);
    REQUIRE(young_id >= 0);
    REQUIRE(no_market_mark_id >= 0);
    const auto late_def = std::find_if(events.begin(), events.end(), [no_market_mark_id](const Event& e) {
        const auto* d = std::get_if<InstrumentDef>(&e.payload);
        return d != nullptr && d->id == no_market_mark_id;
    });
    REQUIRE(late_def != events.end());

    // Delay one real option definition until after its recorded first book. This exercises the
    // valid case where the market state has data before the engine knows how to value it.
    Event delayed_def = *late_def;
    bool delayed_book_seen = false;
    bool delayed_def_applied = false;
    for (const Event& e : events) {
        if (const auto* d = std::get_if<InstrumentDef>(&e.payload);
            d != nullptr && d->id == no_market_mark_id) {
            continue;
        }
        loop.process(e);
        if (const auto* b = std::get_if<BookUpdate>(&e.payload);
            b != nullptr && b->id == no_market_mark_id && !delayed_book_seen) {
            delayed_book_seen = true;
            loop.process(delayed_def);
            delayed_def_applied = true;
        }
    }
    REQUIRE(delayed_book_seen);
    REQUIRE(delayed_def_applied);

    auto best_ask = [&loop](InstrumentId id) -> std::optional<Level> {
        const OrderBook* book = loop.exchange().book(id);
        if (book == nullptr || !book->valid()) return std::nullopt;
        return book->best_ask();
    };
    auto buy_ioc = [&sim, &loop, &best_ask](InstrumentId id, double amount, Nanos when) {
        const auto ask = best_ask(id);
        REQUIRE(ask.has_value());
        OrderRequest order;
        order.account = 0;
        order.client_id = static_cast<std::uint64_t>(id) + 100;
        order.action = OrderAction::Place;
        order.type = OrderType::Ioc;
        order.instrument = id;
        order.side = Side::Buy;
        order.price = ask->price;
        order.amount = amount;
        sim.on_order(order, loop.exchange(), when);
    };

    REQUIRE(best_ask(expired_id).has_value());
    REQUIRE(best_ask(young_id).has_value());
    REQUIRE(best_ask(no_market_mark_id).has_value());
    buy_ioc(expired_id, 0.1, loop.now());
    buy_ioc(no_market_mark_id, 0.1, loop.now());

    // This is the recorded perpetual ticker at data/raw/2026-09-29/00.jsonl.gz line 9.
    constexpr std::string_view perp_ticker_line =
        R"({"recv_ns":1790640000276966000,"msg":{"jsonrpc":"2.0","method":"subscription","params":{"channel":"ticker.BTC-PERPETUAL.100ms","data":{"timestamp":1790640000184,"state":"open","stats":{"high":84996.5,"low":82550.5,"price_change":-1.161,"volume":13719.60796612,"volume_usd":1.1442998e9,"volume_notional":1.1442998e9},"index_price":83460.07,"instrument_name":"BTC-PERPETUAL","last_price":83472.0,"settlement_price":82892.57,"min_price":82223.0,"max_price":84728.0,"open_interest":811318190,"mark_price":83475.42,"interest_value":0.007026081071448403,"current_funding":0.0,"estimated_delivery_price":83460.07,"funding_8h":8.47e-6,"best_ask_price":83472.0,"best_bid_price":83471.5,"best_ask_amount":828320.0,"best_bid_amount":61520.0}}}})";
    std::vector<Event> ticker_events;
    parser.parse_line(perp_ticker_line, ticker_events);
    REQUIRE(ticker_events.size() == 1);
    const auto* recorded_ticker = std::get_if<Ticker>(&ticker_events.front().payload);
    REQUIRE(recorded_ticker != nullptr);
    const InstrumentId perp_id = recorded_ticker->id;
    loop.process(ticker_events.front());

    // The recorded ticker carries the actual best prices. Seed a one-level book from those
    // quotes so the simulator can create a real perpetual position through its normal fill path.
    BookUpdate perp_book;
    perp_book.id = perp_id;
    perp_book.snapshot = true;
    perp_book.change_id = 176389318104;
    perp_book.bids = {{BookAction::New, to_e8(recorded_ticker->best_bid),
                       recorded_ticker->best_bid_amount}};
    perp_book.asks = {{BookAction::New, to_e8(recorded_ticker->best_ask),
                       recorded_ticker->best_ask_amount}};
    loop.process({ticker_events.front().time + 1, ticker_events.front().recv_ns, perp_book});
    buy_ioc(perp_id, 1000.0, loop.now());

    const InstrumentDef* short_def = loop.exchange().def(expired_id);
    REQUIRE(short_def != nullptr);
    const Nanos expiry = short_def->expiry;
    REQUIRE(expiry > loop.now());
    buy_ioc(young_id, 0.1, expiry - 1);  // Its fill remains young across the first option's expiry.
    loop.process({expiry - 1, expiry - 1, IndexPrice{recorded_ticker->index_price}});
    loop.process({expiry + 1, expiry + 1,
                  RecorderEvent{RecorderEvent::Kind::Disconnect}});

    const Account& account = sim.account(0);
    CHECK(account.position(perp_id) > 0);
    CHECK(account.position(expired_id) > 0);
    CHECK(account.position(young_id) > 0);
    CHECK(account.position(no_market_mark_id) > 0);
    CHECK(std::any_of(account.fills().begin(), account.fills().end(), [young_id](const FillRecord& f) {
        return f.instrument == young_id && f.young;
    }));
    CHECK(!std::isfinite(sim.peek_value(no_market_mark_id, loop.exchange())));
    const auto& no_mark_positions = account.positions();
    REQUIRE(static_cast<size_t>(no_market_mark_id) < no_mark_positions.size());
    CHECK(std::isfinite(no_mark_positions[static_cast<size_t>(no_market_mark_id)].last_u));
    CHECK(std::isfinite(sim.peek_value(expired_id, loop.exchange())));  // frozen at expiry

    const PnlRow peek = sim.peek(0, loop.exchange(), loop.now());
    const PnlRow snapshot = sim.snapshot(0, loop.exchange(), loop.now());
    CHECK(peek.time == snapshot.time);
    CHECK(peek.equity == doctest::Approx(snapshot.equity));
    CHECK(peek.cash == doctest::Approx(snapshot.cash));
    CHECK(peek.pnl.total() == doctest::Approx(snapshot.pnl.total()));
    CHECK(peek.pnl.spread == doctest::Approx(snapshot.pnl.spread));
    CHECK(peek.pnl.fees == doctest::Approx(snapshot.pnl.fees));
    CHECK(peek.pnl.adverse == doctest::Approx(snapshot.pnl.adverse));
    CHECK(peek.pnl.option_mtm == doctest::Approx(snapshot.pnl.option_mtm));
    CHECK(peek.pnl.hedge == doctest::Approx(snapshot.pnl.hedge));
    CHECK(peek.delta == doctest::Approx(snapshot.delta));
    CHECK(peek.gamma == doctest::Approx(snapshot.gamma));
    CHECK(peek.vega == doctest::Approx(snapshot.vega));
    CHECK(peek.positions == snapshot.positions);
}

TEST_CASE("zero option mark is retained across two-sided and one-sided book changes") {
    EventLoop loop({0, 0});
    Instruments reg;
    const InstrumentId id = reg.id("BTC-TEST-100-C");
    FeeSchedule fees;
    SimConfig cfg;
    ExchangeSim sim(loop, fees, cfg, {"acct"});
    loop.add_exchange_observer(&sim);

    InstrumentDef def;
    def.id = id;
    def.name = reg.name(id);
    def.kind = InstrumentKind::Option;
    def.is_call = true;
    def.strike = 100000;
    def.expiry = 100 * kSec;
    def.tick_size = 0.0001;
    def.min_trade_amount = 0.1;
    loop.process({kSec, kSec, def});

    Ticker zero_mark;
    zero_mark.id = id;
    zero_mark.mark_price = 0.0;
    loop.process({2 * kSec, 2 * kSec, zero_mark});
    CHECK(sim.unit_value(id, loop.exchange()) == 0.0);

    BookUpdate two_sided;
    two_sided.id = id;
    two_sided.snapshot = true;
    two_sided.change_id = 1;
    two_sided.bids = {{BookAction::New, to_e8(0.002), 1.0}};
    two_sided.asks = {{BookAction::New, to_e8(0.004), 1.0}};
    loop.process({3 * kSec, 3 * kSec, two_sided});
    CHECK(sim.unit_value(id, loop.exchange()) == doctest::Approx(0.003));

    BookUpdate one_sided;
    one_sided.id = id;
    one_sided.snapshot = true;
    one_sided.change_id = 2;
    one_sided.bids = {{BookAction::New, to_e8(0.002), 1.0}};
    loop.process({4 * kSec, 4 * kSec, one_sided});
    CHECK(sim.unit_value(id, loop.exchange()) == 0.0);
}
