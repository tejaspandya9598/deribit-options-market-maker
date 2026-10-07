#include <doctest.h>

#include <cmath>
#include <memory>

#include "event_loop.hpp"
#include "fees.hpp"
#include "hedge_runner.hpp"
#include "hedger.hpp"
#include "portfolio.hpp"
#include "pricing.hpp"
#include "quoter.hpp"
#include "strategy.hpp"

namespace {
using namespace mm;

struct Capture final : ExchangeObserver {
    std::vector<OrderRequest> orders;
    void on_exchange(const Event&, ApplyResult, const MarketState&, Nanos) override {}
    void on_order(const OrderRequest& o, const MarketState&, Nanos) override { orders.push_back(o); }
};

struct ViewSink final : MarketObserver {
    void on_market(const Event&, const MarketState&, Nanos) override {}
};

struct FixedQuote final : Strategy {
    Quote next;
    Quote quote(const QuoteContext&) override { return next; }
    std::string fair_value_source() const override { return "test"; }
    nlohmann::json params() const override { return nlohmann::json::object(); }
};

void seed_option(EventLoop& loop, Nanos expiry = 100 * kSec) {
    InstrumentDef d;
    d.id = 0;
    d.name = "BTC-TEST-100-C";
    d.kind = InstrumentKind::Option;
    d.strike = 100;
    d.is_call = true;
    d.expiry = expiry;
    d.tick_size = 0.0001;
    d.min_trade_amount = 0.1;
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = 0;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(0.01), 1.0}};
    b.asks = {{BookAction::New, to_e8(0.03), 1.0}};
    loop.process({2 * kSec, 2 * kSec, b});
    Ticker k;
    k.id = 0;
    k.mark_price = 0.02;
    k.mark_iv = 80;
    k.underlying_price = 100;
    k.delta = 0.5;
    loop.process({2500 * kMs, 2500 * kMs, k});
}

void add_fit(EventLoop& loop, Nanos expiry = 100 * kSec) {
    SabrFit fit;
    fit.fit_time = 3 * kSec;
    fit.compute_ns = 0;
    fit.expiry = expiry;
    fit.alpha = 8.0;
    fit.beta = 0.5;
    fit.rho = 0.0;
    fit.nu = 0.0;
    fit.recv_clock = true;
    loop.process({fit.fit_time, fit.fit_time, fit});
    loop.finish();
}

QuoteContext context(EventLoop& loop, FeeSchedule* fees = nullptr) {
    QuoteContext c;
    c.now = loop.now();
    c.def = loop.view().def(0);
    c.book = loop.view().book(0);
    c.ticker = loop.view().ticker(0);
    c.forward = 100;
    c.t_years = 1;
    c.mark_iv = 0.8;
    c.max_vega = 100;
    c.fees = fees;
    if (auto it = loop.view().fits().find(c.def->expiry); it != loop.view().fits().end()) c.fit = &it->second;
    return c;
}

void seed_perpetual(EventLoop& loop, InstrumentId id = 1) {
    InstrumentDef d;
    d.id = id;
    d.name = "BTC-PERPETUAL";
    d.kind = InstrumentKind::Perpetual;
    d.tick_size = 0.5;
    d.min_trade_amount = 10;
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = id;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(99990), 10000}};
    b.asks = {{BookAction::New, to_e8(100010), 10000}};
    loop.process({2 * kSec, 2 * kSec, b});
}

OrderUpdate fill_update(std::uint32_t account, InstrumentId id, Side side, double amount,
                        double remaining, std::uint64_t client_id = 100) {
    OrderUpdate u;
    u.account = account;
    u.instrument = id;
    u.kind = UpdateKind::Fill;
    u.side = side;
    u.amount = amount;
    u.remaining = remaining;
    u.client_id = client_id;
    return u;
}
}  // namespace

TEST_CASE("M5 inventory skew moves against signed vega and widens both components") {
    EventLoop loop({0, 0});
    ViewSink sink;
    loop.add_market_observer(&sink);
    seed_option(loop);
    add_fit(loop);
    QuoteContext c = context(loop);
    const double fair_iv = c.sabr_iv();
    REQUIRE(std::isfinite(fair_iv));
    auto strategy = make_inventory_skew(
        {{"half_spread_vp", 2}, {"skew_vp_per_100_vega", 2}, {"inventory_widen_vp", 2},
         {"vol_widen", 0.25}, {"max_vol_widen_vp", 3}});
    c.account_vega = 50;
    c.realized_vol = 1.0;
    const Quote long_vega = strategy->quote(c);
    c.account_vega = -50;
    const Quote short_vega = strategy->quote(c);
    REQUIRE(long_vega.bid);
    REQUIRE(long_vega.ask);
    REQUIRE(short_vega.bid);
    REQUIRE(short_vega.ask);
    CHECK(c.iv_of(long_vega.fair.value()) == doctest::Approx(fair_iv).epsilon(1e-8));
    const double long_centre = (c.iv_of(*long_vega.bid) + c.iv_of(*long_vega.ask)) / 2;
    const double short_centre = (c.iv_of(*short_vega.bid) + c.iv_of(*short_vega.ask)) / 2;
    CHECK(long_centre == doctest::Approx(fair_iv - 0.01).epsilon(1e-7));
    CHECK(short_centre == doctest::Approx(fair_iv + 0.01).epsilon(1e-7));
    CHECK((c.iv_of(*long_vega.ask) - c.iv_of(*long_vega.bid)) == doctest::Approx(0.12).epsilon(1e-7));
    c.realized_vol = 0.7;
    const Quote no_vol_widen = strategy->quote(c);
    CHECK((c.iv_of(*no_vol_widen.ask) - c.iv_of(*no_vol_widen.bid)) == doctest::Approx(0.06).epsilon(1e-7));
}

TEST_CASE("M5 join-best filters each side independently and rejects unsolvable prices") {
    EventLoop loop({0, 0});
    ViewSink sink;
    loop.add_market_observer(&sink);
    seed_option(loop);
    add_fit(loop);
    QuoteContext c = context(loop);
    const double bid_too_close = c.price_btc(0.79);
    const double ask_far = c.price_btc(0.85);
    BookUpdate b;
    b.id = 0;
    b.snapshot = true;
    b.change_id = 2;
    b.bids = {{BookAction::New, to_e8(bid_too_close), 1.0}};
    b.asks = {{BookAction::New, to_e8(ask_far), 1.0}};
    loop.process({4 * kSec, 4 * kSec, b});
    loop.finish();
    c = context(loop);
    auto strategy = make_join_best({{"min_edge_vp", 4}});
    Quote q = strategy->quote(c);
    CHECK_FALSE(q.bid);
    REQUIRE(q.ask);
    CHECK(*q.ask == doctest::Approx(from_e8(to_e8(ask_far))).epsilon(1e-8));

    b.change_id = 3;
    b.bids = {{BookAction::New, to_e8(c.price_btc(0.75)), 1.0}};
    b.asks = {{BookAction::New, to_e8(c.price_btc(0.81)), 1.0}};
    loop.process({5 * kSec, 5 * kSec, b});
    loop.finish();
    c = context(loop);
    q = strategy->quote(c);
    REQUIRE(q.bid);
    CHECK_FALSE(q.ask);

    b.change_id = 4;
    b.bids = {{BookAction::New, to_e8(-0.1), 1.0}};
    b.asks = {{BookAction::New, to_e8(1.1), 1.0}};
    loop.process({6 * kSec, 6 * kSec, b});
    loop.finish();
    c = context(loop);
    q = strategy->quote(c);
    CHECK_FALSE(q.bid);
    CHECK_FALSE(q.ask);
}

TEST_CASE("M5 hedger rules use their bands, intervals, gamma and whole-lot runner sizing") {
    register_builtin_hedgers();
    auto none = make_hedger("none", nlohmann::json::object());
    auto fixed = make_hedger("fixed_band", {{"band_btc", 0.05}});
    auto ww = make_hedger("whalley_wilmott", {{"risk_aversion_per_usd", 0.001}, {"min_band_btc", 0.005}});
    HedgeContext c;
    c.delta = 0.05;
    c.gamma = 0.2;
    c.perp_price = 100000;
    c.cost_rate = 0.00035;
    CHECK(none->trade_delta(c) == 0.0);
    CHECK(fixed->trade_delta(c) == 0.0);  // strict boundary
    c.delta = 0.051;
    CHECK(fixed->trade_delta(c) == doctest::Approx(-0.051));
    const double theoretical = std::cbrt(1.5 * c.cost_rate * c.perp_price * c.gamma * c.gamma / 0.001);
    CHECK(whalley_wilmott_band(c.cost_rate, c.perp_price, c.gamma, 0.001) == doctest::Approx(theoretical));
    auto ww_test = make_hedger("whalley_wilmott", {{"risk_aversion_per_usd", 0.001}, {"min_band_btc", 0.005}});
    c.delta = theoretical + 0.01;
    CHECK(ww_test->trade_delta(c) == doctest::Approx(-0.01));

    auto timed = make_hedger("time_based", {{"interval_min", 15}});
    c.delta = 0.1;
    c.now = kSec;
    CHECK(timed->trade_delta(c) == 0.0);
    c.now = 15 * 60 * kSec;
    CHECK(timed->trade_delta(c) == doctest::Approx(-0.1));
}

TEST_CASE("M5 hedge runner emits the signed perp hedge and waits through partial fill and cancel") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    Capture capture;
    auto runner = std::make_unique<HedgeRunner>(
        loop, 0, make_hedger("fixed_band", {{"band_btc", 0.005}}), fees, HedgeRunnerConfig{});
    loop.add_market_observer(runner.get());
    loop.add_order_observer(runner.get());
    loop.add_exchange_observer(&capture);
    seed_perpetual(loop);
    runner->on_order_update(fill_update(0, 1, Side::Buy, 2000, 0), loop.view(), loop.now());
    Ticker tick;
    tick.id = 1;
    tick.mark_price = 100000;
    loop.process({3 * kSec, 3 * kSec, tick});
    loop.finish();
    REQUIRE(capture.orders.size() == 1);
    const OrderRequest first = capture.orders[0];
    CHECK(first.type == OrderType::Ioc);
    CHECK(first.side == Side::Sell);
    CHECK(first.amount == doctest::Approx(2000));
    CHECK(runner->portfolio().greeks(loop.view(), 1, loop.now()).delta == doctest::Approx(0.02));
    // The sell limit must not go below the configured 0.1% adverse-price bound after tick rounding.
    const Px sell_limit = static_cast<Px>(std::ceil(99990.0 * 0.999 / 0.5)) * to_e8(0.5);
    CHECK(first.price == sell_limit);
    CHECK(first.price == to_e8(99890.5));

    runner->on_order_update(fill_update(0, 1, Side::Sell, 500, 1500, first.client_id), loop.view(),
                            loop.now());
    CHECK(runner->portfolio().greeks(loop.view(), 1, loop.now()).delta == doctest::Approx(0.015));
    OrderUpdate cancelled;
    cancelled.account = 0;
    cancelled.instrument = 1;
    cancelled.client_id = first.client_id;
    cancelled.kind = UpdateKind::Cancelled;
    loop.process({4 * kSec, 4 * kSec, tick});
    loop.finish();
    CHECK(capture.orders.size() == 1);  // a missing terminal update keeps this IOC pending
    runner->on_order_update(cancelled, loop.view(), loop.now());
    loop.process({5 * kSec, 5 * kSec, tick});
    loop.finish();
    REQUIRE(capture.orders.size() == 2);
    CHECK(capture.orders[1].side == Side::Sell);
    CHECK(capture.orders[1].amount == doctest::Approx(1500));
    OrderUpdate rejected;
    rejected.account = 0;
    rejected.instrument = 1;
    rejected.client_id = capture.orders[1].client_id;
    rejected.kind = UpdateKind::Rejected;
    runner->on_order_update(rejected, loop.view(), loop.now());
    loop.process({6 * kSec, 6 * kSec, tick});
    loop.finish();
    REQUIRE(capture.orders.size() == 3);  // reject clears pending, so the next interval retries
    runner->on_order_update(fill_update(0, 1, Side::Sell, 1500, 0, capture.orders.back().client_id),
                            loop.view(), loop.now());
    CHECK(runner->portfolio().greeks(loop.view(), 1, loop.now()).delta == doctest::Approx(0.0));
}

TEST_CASE("M5 hedge slippage rounds buy and sell limits towards the visible best price") {
    register_builtin_hedgers();
    auto place_one = [](Side position_side, double slippage) {
        EventLoop loop({0, 0});
        FeeSchedule fees;
        Capture capture;
        HedgeRunnerConfig cfg;
        cfg.interval = 0;
        cfg.slippage = slippage;
        auto runner = std::make_unique<HedgeRunner>(
            loop, 0, make_hedger("fixed_band", {{"band_btc", 0.005}}), fees, cfg);
        loop.add_market_observer(runner.get());
        loop.add_order_observer(runner.get());
        loop.add_exchange_observer(&capture);
        seed_perpetual(loop);
        runner->on_order_update(fill_update(0, 1, position_side, 2000, 0), loop.view(), loop.now());
        Ticker tick;
        tick.id = 1;
        tick.mark_price = 100000;
        loop.process({3 * kSec, 3 * kSec, tick});
        loop.finish();
        REQUIRE(capture.orders.size() == 1);
        return capture.orders.front();
    };

    // A short perpetual position calls for a buy; the limit cannot exceed the tick below
    // the 0.1%-slippage ceiling.
    const auto buy = place_one(Side::Sell, 0.001);
    REQUIRE(buy.side == Side::Buy);
    const Px buy_ceiling = static_cast<Px>(std::floor(100010.0 * 1.001 / 0.5)) * to_e8(0.5);
    CHECK(buy.price == buy_ceiling);

    const auto zero_slippage_buy = place_one(Side::Sell, 0.0);
    CHECK(zero_slippage_buy.price == to_e8(100010.0));
    const auto zero_slippage_sell = place_one(Side::Buy, 0.0);
    CHECK(zero_slippage_sell.price == to_e8(99990.0));
}

TEST_CASE("M5 hedge runner times out an unanswered IOC and counts a late fill") {
    register_builtin_hedgers();
    EventLoop loop({100 * kMs, 200 * kMs, MdTiming::ExchangeTime, 0});
    FeeSchedule fees;
    Capture capture;
    HedgeRunnerConfig cfg;
    cfg.interval = 0;
    cfg.answer_timeout = 5 * kSec;
    auto runner = std::make_unique<HedgeRunner>(
        loop, 0, make_hedger("fixed_band", {{"band_btc", 0.005}}), fees, cfg);
    loop.add_market_observer(runner.get());
    loop.add_order_observer(runner.get());
    loop.add_exchange_observer(&capture);
    seed_perpetual(loop);
    loop.finish();  // deliver the seeded book before adding inventory to the strategy view
    runner->on_order_update(fill_update(0, 1, Side::Buy, 3000, 0), loop.view(), loop.now());

    Ticker tick;
    tick.id = 1;
    tick.mark_price = 100000;
    loop.process({3 * kSec, 3 * kSec, tick});
    loop.finish();
    REQUIRE(capture.orders.size() == 1);
    const auto abandoned_id = capture.orders[0].client_id;
    CHECK(capture.orders[0].side == Side::Sell);
    CHECK(capture.orders[0].amount == doctest::Approx(3000));

    loop.process({8500 * kMs, 8500 * kMs, tick});  // 5.5 s elapsed; the two trips add 0.6 s
    loop.finish();
    CHECK(capture.orders.size() == 1);
    CHECK(runner->stats().timed_out == 0);

    loop.process({8700 * kMs, 8700 * kMs, tick});  // 5.7 s elapsed, past the 5.6 s deadline
    loop.finish();
    REQUIRE(capture.orders.size() == 2);
    CHECK(runner->stats().timed_out == 1);
    CHECK(capture.orders[1].side == Side::Sell);
    CHECK(capture.orders[1].amount == doctest::Approx(3000));
    const auto active_id = capture.orders[1].client_id;

    // The abandoned IOC's delayed partial fill still changes the portfolio but cannot clear
    // the newer IOC's pending state.
    runner->on_order_update(fill_update(0, 1, Side::Sell, 1000, 2000, abandoned_id), loop.view(), loop.now());
    CHECK(runner->portfolio().greeks(loop.view(), 1, loop.now()).delta == doctest::Approx(0.02));
    CHECK(runner->stats().fills == 1);  // a late hedge fill still belongs in hedge statistics
    CHECK(capture.orders.size() == 2);
    OrderUpdate cancelled;
    cancelled.account = 0;
    cancelled.instrument = 1;
    cancelled.client_id = active_id;
    cancelled.kind = UpdateKind::Cancelled;
    runner->on_order_update(cancelled, loop.view(), loop.now());

    loop.process({9100 * kMs, 9100 * kMs, tick});
    loop.finish();
    REQUIRE(capture.orders.size() == 3);
    CHECK(capture.orders[2].side == Side::Sell);
    CHECK(capture.orders[2].amount == doctest::Approx(2000));
}

TEST_CASE("M5 fee floor and re-quote band are enforced together") {
    EventLoop loop({0, 0});
    Capture capture;
    FeeSchedule fees;
    fees.option_maker = 0.0003;
    fees.option_cap = 0.125;
    auto strategy = std::make_unique<FixedQuote>();
    FixedQuote* strategy_ptr = strategy.get();
    strategy_ptr->next.bid = 0.0199;
    strategy_ptr->next.ask = 0.0201;
    strategy_ptr->next.fair = 0.020;
    QuoterConfig cfg;
    cfg.fee_edge_mult = 1;
    cfg.requote_band_ticks = 1;
    cfg.requote_interval = 0;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 0;
    cfg.stale_after = 60 * kSec;
    Quoter quoter(loop, 0, std::move(strategy), cfg, &fees);
    loop.add_market_observer(&quoter);
    loop.add_order_observer(&quoter);
    loop.add_exchange_observer(&capture);

    InstrumentDef d;
    d.id = 0;
    d.kind = InstrumentKind::Option;
    d.strike = 100;
    d.is_call = true;
    d.expiry = 100 * kSec;
    d.tick_size = 0.001;
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = 0;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(0.01), 1}};
    b.asks = {{BookAction::New, to_e8(0.03), 1}};
    loop.process({2 * kSec, 2 * kSec, b});
    Ticker k;
    k.id = 0;
    k.mark_price = 0.02;
    k.mark_iv = 80;
    k.underlying_price = 100;
    k.delta = 0.5;
    loop.process({2500 * kMs, 2500 * kMs, k});
    loop.process({3 * kSec, 3 * kSec, IndexPrice{100}});
    loop.finish();
    REQUIRE(capture.orders.size() == 2);
    CHECK(capture.orders[0].price == to_e8(0.019));  // fee cap .0197 rounded down
    CHECK(capture.orders[1].price == to_e8(0.021));  // fee floor .0203 rounded up
    for (const auto& o : capture.orders) {
        OrderUpdate accepted;
        accepted.account = 0;
        accepted.instrument = 0;
        accepted.client_id = o.client_id;
        accepted.kind = UpdateKind::Accepted;
        accepted.side = o.side;
        accepted.price = o.price;
        accepted.remaining = o.amount;
        quoter.on_order_update(accepted, loop.view(), loop.now());
    }

    strategy_ptr->next.bid = 0.018;
    strategy_ptr->next.ask = 0.021;
    strategy_ptr->next.fair = 0.020;
    k.mark_price = 0.0201;
    loop.process({4 * kSec, 4 * kSec, k});
    loop.finish();
    CHECK(capture.orders.size() == 2);  // one tick from target and still fee-safe

    strategy_ptr->next.bid = 0.018;
    strategy_ptr->next.ask = 0.021;
    strategy_ptr->next.fair = 0.0185;  // fee bound rounds to .018; resting .019 is now unsafe
    k.mark_price = 0.0185;
    loop.process({5 * kSec, 5 * kSec, k});
    loop.finish();
    REQUIRE(capture.orders.size() == 3);
    CHECK(capture.orders.back().action == OrderAction::Replace);
    CHECK(capture.orders.back().price == to_e8(0.018));
}

TEST_CASE("M5 fee-floor safety overrides the re-quote band across a tick-step boundary") {
    EventLoop loop({0, 0});
    Capture capture;
    FeeSchedule fees;
    fees.option_maker = 0.0003;
    fees.option_cap = 0.125;
    auto strategy = std::make_unique<FixedQuote>();
    FixedQuote* strategy_ptr = strategy.get();
    strategy_ptr->next.bid = 0.03;
    strategy_ptr->next.fair = 0.0205;
    QuoterConfig cfg;
    cfg.fee_edge_mult = 1;
    cfg.requote_band_ticks = 1;
    cfg.requote_interval = 0;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 0;
    cfg.stale_after = 60 * kSec;
    Quoter quoter(loop, 0, std::move(strategy), cfg, &fees);
    loop.add_market_observer(&quoter);
    loop.add_order_observer(&quoter);
    loop.add_exchange_observer(&capture);

    InstrumentDef d;
    d.id = 0;
    d.kind = InstrumentKind::Option;
    d.strike = 100;
    d.is_call = true;
    d.expiry = 100 * kSec;
    d.tick_size = 0.001;
    d.tick_steps = {{0.02, 0.005}};
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = 0;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(0.01), 1}};
    b.asks = {{BookAction::New, to_e8(0.03), 1}};
    loop.process({2 * kSec, 2 * kSec, b});
    Ticker k;
    k.id = 0;
    k.mark_price = 0.0205;
    k.mark_iv = 80;
    k.underlying_price = 100;
    k.delta = 0.5;
    loop.process({3 * kSec, 3 * kSec, k});
    loop.finish();
    REQUIRE(capture.orders.size() == 1);
    CHECK(capture.orders[0].price == to_e8(0.020));  // cap is just over the 0.02 step boundary

    OrderUpdate accepted;
    accepted.account = 0;
    accepted.instrument = 0;
    accepted.client_id = capture.orders[0].client_id;
    accepted.kind = UpdateKind::Accepted;
    accepted.side = Side::Buy;
    accepted.price = capture.orders[0].price;
    accepted.remaining = capture.orders[0].amount;
    quoter.on_order_update(accepted, loop.view(), loop.now());

    strategy_ptr->next.fair = 0.0202;  // fee-safe cap moves one base tick below the resting quote
    k.mark_price = 0.0202;
    loop.process({4 * kSec, 4 * kSec, k});
    loop.finish();
    REQUIRE(capture.orders.size() == 2);
    CHECK(capture.orders[1].action == OrderAction::Replace);
    CHECK(capture.orders[1].price == to_e8(0.019));
}

TEST_CASE("M5 near-expiry quotes only reduce inventory and trading-window edges are exact") {
    EventLoop loop({0, 0});
    Capture capture;
    auto strategy = std::make_unique<FixedQuote>();
    FixedQuote* strategy_ptr = strategy.get();
    strategy_ptr->next.bid = 0.02;
    strategy_ptr->next.ask = 0.025;
    strategy_ptr->next.fair = 0.022;
    QuoterConfig cfg;
    cfg.requote_interval = 0;
    cfg.stale_after = 60 * kSec;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 5 * kSec;
    cfg.trade_from = 3 * kSec;
    cfg.trade_until = 18 * kSec;
    Quoter quoter(loop, 0, std::move(strategy), cfg);
    loop.add_market_observer(&quoter);
    loop.add_order_observer(&quoter);
    loop.add_exchange_observer(&capture);
    InstrumentDef d;
    d.id = 0;
    d.kind = InstrumentKind::Option;
    d.strike = 100;
    d.is_call = true;
    d.expiry = 20 * kSec;
    d.tick_size = 0.001;
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = 0;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(0.01), 1}};
    b.asks = {{BookAction::New, to_e8(0.03), 1}};
    loop.process({2 * kSec, 2 * kSec, b});
    Ticker k;
    k.id = 0;
    k.mark_price = 0.02;
    k.mark_iv = 80;
    k.underlying_price = 100;
    k.delta = 0.5;
    loop.process({2500 * kMs, 2500 * kMs, k});
    loop.process({3 * kSec, 3 * kSec, k});  // exact trade_from is eligible
    loop.finish();
    REQUIRE(capture.orders.size() == 2);
    for (const auto& o : capture.orders) {
        OrderUpdate accepted;
        accepted.account = 0;
        accepted.instrument = 0;
        accepted.client_id = o.client_id;
        accepted.kind = UpdateKind::Accepted;
        accepted.side = o.side;
        accepted.price = o.price;
        accepted.remaining = o.amount;
        quoter.on_order_update(accepted, loop.view(), loop.now());
    }
    quoter.on_order_update(fill_update(0, 0, Side::Buy, 0.1, 0), loop.view(), loop.now());

    // At expiry-minus-five, long inventory keeps only the sell that reduces it.
    strategy_ptr->next.bid = 0.02;
    strategy_ptr->next.ask = 0.025;
    loop.process({15 * kSec, 15 * kSec, k});
    loop.finish();
    CHECK(capture.orders.size() == 3);
    CHECK(capture.orders.back().action == OrderAction::Cancel);
    CHECK(capture.orders.back().side == Side::Buy);

    // The exact trade_until event pulls the remaining quote and cannot place another side.
    loop.process({18 * kSec, 18 * kSec, k});
    loop.finish();
    CHECK(capture.orders.size() == 4);
    CHECK(capture.orders.back().action == OrderAction::Cancel);
    CHECK(capture.orders.back().side == Side::Sell);
}

TEST_CASE("M5 near-expiry mode leaves a flat account unquoted") {
    EventLoop loop({0, 0});
    Capture capture;
    auto strategy = std::make_unique<FixedQuote>();
    strategy->next.bid = 0.02;
    strategy->next.ask = 0.025;
    strategy->next.fair = 0.022;
    QuoterConfig cfg;
    cfg.requote_interval = 0;
    cfg.stale_after = 60 * kSec;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 5 * kSec;
    Quoter quoter(loop, 0, std::move(strategy), cfg);
    loop.add_market_observer(&quoter);
    loop.add_exchange_observer(&capture);
    InstrumentDef d;
    d.id = 0;
    d.kind = InstrumentKind::Option;
    d.strike = 100;
    d.is_call = true;
    d.expiry = 20 * kSec;
    d.tick_size = 0.001;
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = 0;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(0.01), 1}};
    b.asks = {{BookAction::New, to_e8(0.03), 1}};
    loop.process({2 * kSec, 2 * kSec, b});
    Ticker k;
    k.id = 0;
    k.mark_price = 0.02;
    k.mark_iv = 80;
    k.underlying_price = 100;
    k.delta = 0.5;
    loop.process({15 * kSec, 15 * kSec, k});  // exactly expiry minus the reduce-only window
    loop.finish();
    CHECK(capture.orders.empty());
}

TEST_CASE("M5 realized volatility ignores late index events") {
    EventLoop loop({0, 0});
    auto strategy = std::make_unique<FixedQuote>();
    QuoterConfig cfg;
    cfg.rv_halflife = 10 * kSec;
    cfg.requote_interval = 0;
    Quoter quoter(loop, 0, std::move(strategy), cfg);
    loop.add_market_observer(&quoter);
    loop.process({kSec, kSec, IndexPrice{100}});
    loop.process({11 * kSec, 11 * kSec, IndexPrice{110}});
    loop.finish();
    const double on_time = quoter.realized_vol();
    REQUIRE(std::isfinite(on_time));
    loop.process({5 * kSec, 12 * kSec, IndexPrice{1}});
    CHECK(quoter.realized_vol() == doctest::Approx(on_time));
}

TEST_CASE("M5 hedge runner sees the perp book only after market-data latency") {
    EventLoop loop({100 * kMs, 0, MdTiming::ExchangeTime, 0});
    FeeSchedule fees;
    Capture capture;
    auto runner = std::make_unique<HedgeRunner>(
        loop, 0, make_hedger("fixed_band", {{"band_btc", 0.005}}), fees, HedgeRunnerConfig{});
    loop.add_market_observer(runner.get());
    loop.add_order_observer(runner.get());
    loop.add_exchange_observer(&capture);
    InstrumentDef d;
    d.id = 1;
    d.name = "BTC-PERPETUAL";
    d.kind = InstrumentKind::Perpetual;
    d.tick_size = 0.5;
    d.min_trade_amount = 10;
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = 1;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, to_e8(99990), 10000}};
    b.asks = {{BookAction::New, to_e8(100010), 10000}};
    loop.process({2 * kSec, 2 * kSec, b});
    runner->on_order_update(fill_update(0, 1, Side::Buy, 2000, 0), loop.view(), loop.now());
    CHECK(capture.orders.empty());
    loop.process({2099 * kMs, 2099 * kMs, IndexPrice{100000}});
    CHECK(capture.orders.empty());
    loop.process({2100 * kMs, 2100 * kMs, IndexPrice{100000}});
    loop.finish();
    REQUIRE(capture.orders.size() == 1);
}

TEST_CASE("M5 hedge runner waits for the required perp side and keeps pending hedge through option settlement") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    Capture capture;
    HedgeRunnerConfig cfg;
    cfg.interval = 0;
    auto runner = std::make_unique<HedgeRunner>(
        loop, 0, make_hedger("fixed_band", {{"band_btc", 0.005}}), fees, cfg);
    loop.add_market_observer(runner.get());
    loop.add_order_observer(runner.get());
    loop.add_exchange_observer(&capture);
    seed_option(loop);
    seed_perpetual(loop);

    // A long option position asks the runner to sell, but a one-sided book has no bid.
    runner->on_order_update(fill_update(0, 0, Side::Buy, 1.0, 0), loop.view(), loop.now());
    CHECK(runner->stats().fills == 0);  // option fills update inventory, not hedge-order stats
    BookUpdate one_sided;
    one_sided.id = 1;
    one_sided.prev_change_id = 1;
    one_sided.change_id = 2;
    one_sided.bids = {{BookAction::Delete, to_e8(99990), 0}};
    loop.process({3 * kSec, 3 * kSec, one_sided});
    loop.finish();
    Ticker perp_mark;
    perp_mark.id = 1;
    perp_mark.mark_price = 100000;
    loop.process({3100 * kMs, 3100 * kMs, perp_mark});
    loop.finish();
    CHECK(capture.orders.empty());

    // Restore both sides; this emits an IOC. The option settles while that IOC is pending.
    BookUpdate restored;
    restored.id = 1;
    restored.prev_change_id = 2;
    restored.change_id = 3;
    restored.bids = {{BookAction::New, to_e8(99990), 10000}};
    loop.process({3200 * kMs, 3200 * kMs, restored});
    loop.finish();
    REQUIRE(capture.orders.size() == 1);
    const auto hedge_id = capture.orders.front().client_id;

    OrderUpdate settled;
    settled.account = 0;
    settled.instrument = 0;
    settled.kind = UpdateKind::Settled;
    runner->on_order_update(settled, loop.view(), loop.now());
    BookUpdate next;
    next.id = 1;
    next.prev_change_id = 3;
    next.change_id = 4;
    next.bids = {{BookAction::Change, to_e8(99990), 10000}};
    loop.process({4200 * kMs, 4200 * kMs, next});
    loop.finish();
    CHECK(capture.orders.size() == 1);  // settlement does not release the in-flight IOC

    OrderUpdate cancelled;
    cancelled.account = 0;
    cancelled.instrument = 1;
    cancelled.client_id = hedge_id;
    cancelled.kind = UpdateKind::Cancelled;
    runner->on_order_update(cancelled, loop.view(), loop.now());
    loop.process({5200 * kMs, 5200 * kMs, next});
    loop.finish();
    CHECK(capture.orders.size() == 1);  // now clear, but there is no settled option delta to hedge
}

TEST_CASE("M5 time-based hedger does not fire before its first interval") {
    register_builtin_hedgers();
    auto timed = make_hedger("time_based", {{"interval_min", 15}});
    HedgeContext c;
    c.delta = 0.1;
    c.now = kSec;
    CHECK(timed->trade_delta(c) == 0.0);
    c.now = 15 * 60 * kSec - kSec;
    CHECK(timed->trade_delta(c) == 0.0);
    c.now = 15 * 60 * kSec;
    CHECK(timed->trade_delta(c) == doctest::Approx(-0.1));
    c.now = 30 * 60 * kSec;
    CHECK(timed->trade_delta(c) == doctest::Approx(-0.1));
}
