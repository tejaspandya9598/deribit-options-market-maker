#include <doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

#include "checkpoint.hpp"
#include "event_loop.hpp"
#include "instruments.hpp"
#include "input.hpp"
#include "parse.hpp"

namespace {

using namespace mm;

BookUpdate snapshot(InstrumentId id, std::int64_t cid, Px bid, Px ask) {
    return {id, true, cid, -1, {{BookAction::New, bid, 1.0}}, {{BookAction::New, ask, 2.0}}};
}

BookUpdate change(InstrumentId id, std::int64_t cid, std::int64_t prev, Px bid,
                  double amount) {
    return {id, false, cid, prev, {{BookAction::Change, bid, amount}}, {}};
}

struct ProbeObserver final : MarketObserver, ExchangeObserver {
    EventLoop* loop = nullptr;
    std::vector<Nanos> market_times;
    std::vector<Nanos> order_times;
    Nanos first_event_time = 0;
    bool first_event_visible = false;

    void on_market(const Event& e, const MarketState&, Nanos now) override {
        market_times.push_back(now);
        if (e.time == first_event_time) {
            first_event_visible = true;
            loop->submit_order({});
        }
    }
    void on_exchange(const Event&, ApplyResult, const MarketState&, Nanos) override {}
    void on_order(const OrderRequest&, const MarketState&, Nanos now) override {
        order_times.push_back(now);
    }
};

}  // namespace

TEST_CASE("event loop enforces the latency range") {
    CHECK_THROWS_AS(EventLoop({-1, 0}), std::invalid_argument);
    CHECK_THROWS_AS(EventLoop({0, -1}), std::invalid_argument);
    CHECK_NOTHROW(EventLoop({0, 0}));
    CHECK_NOTHROW(EventLoop({kMaxLatency, kMaxLatency}));
    CHECK_THROWS_AS(EventLoop({kMaxLatency + 1, 0}), std::invalid_argument);
    CHECK_THROWS_AS(EventLoop({0, kMaxLatency + 1}), std::invalid_argument);
    CHECK_THROWS_AS(EventLoop({kMaxLatency + kMs, 0}), std::invalid_argument);
    CHECK_THROWS_AS(EventLoop({0, kMaxLatency + kMs}), std::invalid_argument);
    CHECK_THROWS_AS(EventLoop({std::numeric_limits<Nanos>::max(), 0}), std::invalid_argument);
    CHECK_THROWS_AS(EventLoop({0, std::numeric_limits<Nanos>::max()}), std::invalid_argument);
}

TEST_CASE("book validity follows snapshots, chained changes, and connection invalidation") {
    Instruments reg;
    const auto id = reg.id("BTC-X");
    MarketState state;
    CHECK(state.apply({100, 100, change(id, 11, 10, 100, 3.0)}) == ApplyResult::NoSnapshot);
    CHECK_FALSE(state.book(id)->valid());
    CHECK(state.apply({110, 110, snapshot(id, 20, 100, 110)}) == ApplyResult::Snapshot);
    CHECK(state.apply({120, 120, change(id, 21, 20, 100, 4.0)}) == ApplyResult::Ok);
    CHECK(state.book(id)->valid());
    CHECK(state.apply({130, 130, change(id, 22, 19, 100, 5.0)}) == ApplyResult::Gap);
    CHECK_FALSE(state.book(id)->valid());

    state.apply({140, 140, snapshot(id, 30, 101, 111)});
    state.apply({150, 150, RecorderEvent{RecorderEvent::Kind::Disconnect}});
    CHECK_FALSE(state.book(id)->valid());
    CHECK(state.apply({160, 160, change(id, 31, 30, 101, 6.0)}) == ApplyResult::NoSnapshot);
    CHECK_FALSE(state.book(id)->valid());
    CHECK(state.apply({170, 170, snapshot(id, 40, 102, 112)}) == ApplyResult::Snapshot);
    state.apply({180, 180, RecorderEvent{RecorderEvent::Kind::Connect}});
    CHECK_FALSE(state.book(id)->valid());
}

TEST_CASE("market events and orders are released only after their configured latency") {
    constexpr Nanos md_latency = 20;
    constexpr Nanos order_latency = 7;
    EventLoop loop({md_latency, order_latency});
    ProbeObserver observer;
    observer.loop = &loop;
    observer.first_event_time = 100;
    loop.add_market_observer(&observer);
    loop.add_exchange_observer(&observer);

    loop.process({100, 1, IndexPrice{1.0}});
    CHECK(observer.market_times.empty());
    CHECK(loop.view().index_price() != 1.0);
    loop.process({119, 2, IndexPrice{2.0}});
    CHECK(observer.market_times.empty());
    CHECK(loop.view().index_price() != 1.0);

    // Processing an event at 120 releases the first event at exactly T + md_latency.
    loop.process({120, 3, IndexPrice{3.0}});
    REQUIRE(observer.market_times.size() == 1);
    CHECK(observer.market_times[0] == 120);
    CHECK(observer.first_event_visible);
    CHECK(observer.order_times.empty());

    // The order submitted at 120 cannot reach the exchange before 120 + order_latency.
    loop.process({126, 4, IndexPrice{4.0}});
    CHECK(observer.order_times.empty());
    loop.process({127, 5, IndexPrice{5.0}});
    REQUIRE(observer.order_times.size() == 1);
    CHECK(observer.order_times[0] == 127);
}

TEST_CASE("fit input is parsed at fit_time plus compute_ns and merged before later market data") {
    std::unique_ptr<std::FILE, decltype(&std::fclose)> main_file(std::tmpfile(), &std::fclose);
    std::unique_ptr<std::FILE, decltype(&std::fclose)> fit_file(std::tmpfile(), &std::fclose);
    REQUIRE(main_file != nullptr);
    REQUIRE(fit_file != nullptr);
    std::fputs(
        "{\"recv_ns\":1,\"msg\":{\"jsonrpc\":\"2.0\",\"method\":\"subscription\","
        "\"params\":{\"channel\":\"deribit_price_index.btc_usd\",\"data\":{\"timestamp\":1000,"
        "\"price\":80000}}}}\n"
        "{\"recv_ns\":2,\"msg\":{\"jsonrpc\":\"2.0\",\"method\":\"subscription\","
        "\"params\":{\"channel\":\"deribit_price_index.btc_usd\",\"data\":{\"timestamp\":1001,"
        "\"price\":80001}}}}\n",
        main_file.get());
    std::fputs(
        "{\"fit_time_ns\":1000000000,\"compute_ns\":200,\"expiry_ms\":2000,"
        "\"beta\":0.5,\"alpha\":0.8,\"rho\":-0.2,\"nu\":0.6,"
        "\"forward\":80000,\"rmse_vol\":0.01}\n",
        fit_file.get());
    std::rewind(main_file.get());
    std::rewind(fit_file.get());

    Instruments reg;
    Parser parser(reg);
    LineReader main_reader(main_file.get());
    LineReader fit_reader(fit_file.get());
    MergedInput input(main_reader, &fit_reader, parser);
    Event first, second, third;
    REQUIRE(input.next(first));
    REQUIRE(input.next(second));
    REQUIRE(input.next(third));
    CHECK(std::holds_alternative<IndexPrice>(first.payload));
    CHECK(std::holds_alternative<SabrFit>(second.payload));
    CHECK(second.time == 1'000'000'200);
    CHECK(std::get<SabrFit>(second.payload).expiry == 2 * kSec);
    CHECK(std::holds_alternative<IndexPrice>(third.payload));
    CHECK(third.time == 1'001 * kMs);
    CHECK_FALSE(input.next(first));
}

TEST_CASE("checkpoint replay preserves books from the checkpoint hour") {
    constexpr Nanos hour = 3'600 * kSec;
    auto root = std::filesystem::temp_directory_path() /
                ("deribit-mm-m2-checkpoint-test-" + std::to_string(getpid()));

    Instruments reg;
    Parser parser(reg);
    const auto id = reg.id("BTC-27SEP26-50000-C");
    EventLoop full({0, 0});
    CheckpointWriter writer(root, reg, parser);

    writer.before(hour - 1, full.exchange());
    full.process({hour - 2, 10, snapshot(id, 100, 400, 500)});
    full.process({hour - 1, 11, change(id, 101, 100, 400, 3.0)});
    writer.before(hour, full.exchange());
    const auto checkpoint = root / "1970-01-01" / "01.jsonl";
    REQUIRE(std::filesystem::exists(checkpoint));

    std::ifstream in(checkpoint);
    std::string line;
    REQUIRE(static_cast<bool>(std::getline(in, line)));
    Instruments replay_reg;
    Parser replay_parser(replay_reg);
    std::vector<Event> parsed;
    replay_parser.parse_line(line, parsed);
    REQUIRE(parsed.size() == 1);
    const auto* synthetic = std::get_if<BookUpdate>(&parsed[0].payload);
    REQUIRE(synthetic != nullptr);
    CHECK(synthetic->snapshot);
    CHECK(synthetic->change_id == 101);

    EventLoop replay({0, 0});
    replay.process(std::move(parsed[0]));
    replay.process({hour + 1, hour + 1, change(replay_reg.id("BTC-27SEP26-50000-C"), 102, 101,
                                                 400, 4.0)});
    full.process({hour + 1, hour + 1, change(id, 102, 101, 400, 4.0)});

    const auto* full_book = full.exchange().book(id);
    const auto* replay_book = replay.exchange().book(0);
    REQUIRE(full_book != nullptr);
    REQUIRE(replay_book != nullptr);
    CHECK(full_book->valid());
    CHECK(replay_book->valid());
    CHECK(full_book->change_id() == replay_book->change_id());
    REQUIRE(full_book->best_bid().has_value());
    REQUIRE(replay_book->best_bid().has_value());
    CHECK(full_book->best_bid()->price == replay_book->best_bid()->price);
    CHECK(full_book->best_bid()->amount == replay_book->best_bid()->amount);
    REQUIRE(full_book->best_ask().has_value());
    REQUIRE(replay_book->best_ask().has_value());
    CHECK(full_book->best_ask()->price == replay_book->best_ask()->price);
    CHECK(full_book->best_ask()->amount == replay_book->best_ask()->amount);

    std::filesystem::remove_all(root);
}

TEST_CASE("checkpoint re-stamps the original instrument-list message") {
    constexpr Nanos hour = 3'600 * kSec;
    auto root = std::filesystem::temp_directory_path() /
                ("deribit-mm-m2-checkpoint-def-test-" + std::to_string(getpid()));

    Instruments reg;
    Parser parser(reg);
    EventLoop loop({0, 0});
    CheckpointWriter writer(root, reg, parser);
    writer.before(hour - 1, loop.exchange());

    const std::string input =
        "{\"recv_ns\":3599999999900,\"msg\":{\"jsonrpc\":\"2.0\",\"id\":7,"
        "\"result\":[{\"instrument_name\":\"BTC-RAW-MSG\"}]}}";
    std::vector<Event> parsed;
    parser.parse_line(input, parsed);
    REQUIRE(parsed.size() == 1);
    loop.process(std::move(parsed.front()));
    writer.before(hour + 10, loop.exchange());

    const auto checkpoint = root / "1970-01-01" / "01.jsonl";
    std::ifstream in(checkpoint);
    std::string line;
    REQUIRE(static_cast<bool>(std::getline(in, line)));
    CHECK(line ==
          "{\"recv_ns\":3600000000000,\"checkpoint\":true,\"msg\":"
          "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":[{\"instrument_name\":\"BTC-RAW-MSG\"}]}}");

    std::filesystem::remove_all(root);
}

TEST_CASE("checkpoint follows receive hour when exchange update time is earlier") {
    constexpr Nanos boundary = 3'600 * kSec;
    constexpr Nanos after_boundary = boundary + 25 * kMs;
    auto root = std::filesystem::temp_directory_path() /
                ("deribit-mm-m2-recv-hour-test-" + std::to_string(getpid()));

    Instruments reg;
    Parser parser(reg);
    const auto id = reg.id("BTC-RECV-HOUR");
    EventLoop full({0, 0});
    CheckpointWriter writer(root, reg, parser);
    writer.before(boundary - 1, full.exchange());

    full.process({boundary - 300 * kMs, boundary - 300 * kMs,
                  snapshot(id, 100, 400, 500)});
    full.process({boundary - 150 * kMs, boundary - 150 * kMs,
                  change(id, 101, 100, 400, 3.0)});

    Event late_received{boundary - 50 * kMs, after_boundary,
                        change(id, 102, 101, 400, 4.0)};
    REQUIRE(late_received.time < boundary);
    REQUIRE(late_received.recv_ns >= boundary);
    writer.before(late_received.recv_ns, full.exchange());
    full.process(late_received);

    const auto checkpoint = root / "1970-01-01" / "01.jsonl";
    REQUIRE(std::filesystem::exists(checkpoint));
    CHECK_FALSE(std::filesystem::exists(root / "1970-01-01" / "00.jsonl"));

    std::ifstream in(checkpoint);
    std::string line;
    REQUIRE(static_cast<bool>(std::getline(in, line)));
    CHECK(line.find("\"recv_ns\":3600000000000") != std::string::npos);

    Instruments replay_reg;
    Parser replay_parser(replay_reg);
    std::vector<Event> parsed;
    replay_parser.parse_line(line, parsed);
    REQUIRE(parsed.size() == 1);
    const auto* saved = std::get_if<BookUpdate>(&parsed.front().payload);
    REQUIRE(saved != nullptr);
    CHECK(saved->snapshot);
    CHECK(saved->change_id == 101);

    EventLoop replay({0, 0});
    replay.process(std::move(parsed.front()));
    const auto replay_id = replay_reg.id("BTC-RECV-HOUR");
    replay.process({late_received.time, late_received.recv_ns,
                    change(replay_id, 102, 101, 400, 4.0)});

    const auto* full_book = full.exchange().book(id);
    const auto* replay_book = replay.exchange().book(replay_id);
    REQUIRE(full_book != nullptr);
    REQUIRE(replay_book != nullptr);
    CHECK(full_book->valid());
    CHECK(replay_book->valid());
    CHECK(full_book->change_id() == 102);
    CHECK(replay_book->change_id() == full_book->change_id());
    REQUIRE(full_book->best_bid().has_value());
    REQUIRE(replay_book->best_bid().has_value());
    CHECK(replay_book->best_bid()->price == full_book->best_bid()->price);
    CHECK(replay_book->best_bid()->amount == full_book->best_bid()->amount);

    std::filesystem::remove_all(root);
}
