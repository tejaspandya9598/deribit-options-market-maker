#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "account.hpp"
#include "event_loop.hpp"
#include "exchange_sim.hpp"
#include "fees.hpp"
#include "fill_model.hpp"
#include "quoter.hpp"
#include "strategy.hpp"

namespace {
using namespace mm;

Resting joined(const FillModel& model, double ahead = 1.0) {
    Resting o;
    o.remaining = 1.0;
    model.on_join(o, ahead);
    return o;
}

struct TimeProbe final : MarketObserver {
    std::vector<Nanos> times;
    std::vector<Nanos> event_times;
    void on_market(const Event& e, const MarketState&, Nanos now) override {
        times.push_back(now);
        event_times.push_back(e.time);
    }
};

struct CaptureExchange final : ExchangeObserver {
    std::vector<OrderRequest> orders;
    void on_exchange(const Event&, ApplyResult, const MarketState&, Nanos) override {}
    void on_order(const OrderRequest& o, const MarketState&, Nanos) override { orders.push_back(o); }
};

struct TestStrategy final : Strategy {
    Quote next;
    Quote quote(const QuoteContext&) override { return next; }
    std::string fair_value_source() const override { return "test"; }
    nlohmann::json params() const override { return nlohmann::json::object(); }
};

void seed_option(EventLoop& loop, InstrumentId id = 0, Nanos expiry = 100 * kSec) {
    InstrumentDef d;
    d.id = id;
    d.name = "BTC-TEST-100-C";
    d.kind = InstrumentKind::Option;
    d.is_call = true;
    d.strike = 100;
    d.expiry = expiry;
    d.tick_size = 0.0001;
    d.min_trade_amount = 0.1;
    d.settlement_period = "day";
    loop.process({kSec, kSec, d});
    BookUpdate b;
    b.id = id;
    b.snapshot = true;
    b.change_id = 1;
    b.bids = {{BookAction::New, 100, 1.0}};
    b.asks = {{BookAction::New, 120, 1.0}};
    loop.process({2 * kSec, 2 * kSec, b});
    Ticker k;
    k.id = id;
    k.mark_price = 0.00011;
    k.mark_iv = 80;
    k.underlying_price = 100;
    k.delta = 0.5;
    loop.process({2500 * kMs, 2500 * kMs, k});
}

void pulse(EventLoop& loop, Nanos t) { loop.process({t, t, IndexPrice{100}}); }
}  // namespace

TEST_CASE("M4 fill models respect queue, partial trades, and both channel orders") {
    auto q = make_fill_model(FillModelKind::QueueAware, TradeThrough::TradeSize, 100 * kMs);

    SUBCASE("trade arrives before its level reduction") {
        auto o = joined(*q);
        CHECK(q->on_trade(o, false, 0.4, 0.4, 10) == doctest::Approx(0.0));
        q->on_level(o, 0.6, 10);
        CHECK(o.queue_ahead == doctest::Approx(0.6));
        CHECK(o.trade_credit == doctest::Approx(0.0));
    }
    SUBCASE("level reduction arrives before its matching trade") {
        auto o = joined(*q);
        q->on_level(o, 0.6, 10);
        CHECK(o.queue_ahead == doctest::Approx(0.6));
        CHECK(q->on_trade(o, false, 0.4, 0.4, 10) == doctest::Approx(0.0));
        CHECK(o.queue_ahead == doctest::Approx(0.6));
        CHECK(o.guess_cancel == doctest::Approx(0.0));
    }
    SUBCASE("a print overlapping a partial cancel does not consume the drop twice") {
        auto o = joined(*q);
        q->on_level(o, 0.7, 10);  // 0.3 drop provisionally treated as a cancel
        CHECK(q->on_trade(o, false, 0.5, 0.5, 10) == doctest::Approx(0.0));
        CHECK(o.queue_ahead == doctest::Approx(0.5));
        q->on_level(o, 0.5, 10);  // matching 0.5 trade drop
        CHECK(o.queue_ahead == doctest::Approx(0.5));
    }
    SUBCASE("queue clears at a through trade, but the configured amount is respected") {
        auto o = joined(*q);
        CHECK(q->on_trade(o, true, 0.2, 0.2, 10) == doctest::Approx(0.2));
        auto full = make_fill_model(FillModelKind::QueueAware, TradeThrough::Full, 100 * kMs);
        auto all = joined(*full);
        CHECK(full->on_trade(all, true, 0.2, 0.2, 10) == doctest::Approx(1.0));
    }
}

TEST_CASE("M4 optimistic fill is never smaller than queue-aware for both trade-through settings") {
    for (TradeThrough through : {TradeThrough::TradeSize, TradeThrough::Full}) {
        auto optimistic = make_fill_model(FillModelKind::Optimistic, through, kSec);
        auto queue = make_fill_model(FillModelKind::QueueAware, through, kSec);
        for (bool is_through : {false, true}) {
            for (double queue_size : {0.0, 0.1, 0.7, 1.0}) {
                for (double amount : {0.1, 0.5, 1.0, 1.5}) {
                    auto a = joined(*optimistic, queue_size);
                    auto b = joined(*queue, queue_size);
                    const double of = optimistic->on_trade(a, is_through, amount, amount, 1);
                    const double qf = queue->on_trade(b, is_through, amount, amount, 1);
                    CHECK(of + 1e-12 >= qf);
                }
            }
        }
    }
}

TEST_CASE("M4 market data is not delivered before event time plus latency in either mode") {
    for (MdTiming mode : {MdTiming::ExchangeTime, MdTiming::AsReceived}) {
        constexpr Nanos t = 10 * kSec;
        constexpr Nanos md = 20 * kMs;
        constexpr Nanos recorded = 35 * kMs;
        EventLoop loop({md, 0, mode, recorded});
        TimeProbe probe;
        loop.add_market_observer(&probe);
        const Nanos recv = mode == MdTiming::AsReceived ? t + 100 * kMs : 0;
        loop.process({t, recv, IndexPrice{80000}});
        loop.process({t + md - 1, 0, IndexPrice{80001}});
        CHECK(probe.times.empty());
        loop.process({t + 200 * kMs, 0, IndexPrice{80002}});
        loop.finish();
        REQUIRE(probe.times.size() == 3);
        for (size_t i = 0; i < probe.times.size(); ++i) {
            CHECK(probe.times[i] >= probe.event_times[i] + md);
        }
    }
}

TEST_CASE("M4 account equity reconciles with fills, marks, markouts, and fees") {
    Account account("test", 1.0);
    FillRecord f;
    f.instrument = 0;
    f.option = true;
    f.side = Side::Buy;
    f.amount = 0.5;
    f.u_fill = 0.01;
    f.u_mid = 0.011;
    f.fee = 0.0001;
    const auto i = account.fill(f);
    CHECK(account.equity() - 1.0 == doctest::Approx(account.pnl().total()).epsilon(1e-12));
    account.markout(i, 0, 0.012);
    account.markout(i, kAdverseHorizon, 0.009);
    CHECK(account.equity() - 1.0 == doctest::Approx(account.pnl().total()).epsilon(1e-12));
    account.mark(0, 0.008);
    CHECK(account.equity() - 1.0 == doctest::Approx(account.pnl().total()).epsilon(1e-12));
    account.settle(0, 0.007, 0.00002);
    CHECK(account.equity() - 1.0 == doctest::Approx(account.pnl().total()).epsilon(1e-12));
    CHECK(account.position(0) == 0.0);
}

TEST_CASE("M4 fee schedule applies option cap, inverse perpetual, and settlement period") {
    FeeSchedule f;
    f.option_maker = f.option_taker = 0.0003;
    f.option_cap = 0.125;
    f.perp_taker = 0.00035;
    f.delivery["day"] = {0.0, 0.125};
    f.delivery["week"] = {0.00015, 0.125};
    CHECK(f.option_fee(0.001, 1.0, true) == doctest::Approx(0.000125));
    CHECK(f.option_fee(0.01, 2.0, false) == doctest::Approx(0.0006));
    CHECK(f.perp_fee(80000, 80000, false) == doctest::Approx(0.00035));
    CHECK(f.delivery_fee("day", 0.01, 1.0) == doctest::Approx(0.0));
    CHECK(f.delivery_fee("week", 0.01, 2.0) == doctest::Approx(0.0003));
}

TEST_CASE("M4 fixed spread follows SABR and mark-IV follower follows the ticker mark") {
    register_builtin_strategies();
    InstrumentDef def;
    def.kind = InstrumentKind::Option;
    def.strike = 100.0;
    def.is_call = true;
    Ticker ticker;
    ticker.mark_iv = 80.0;
    SabrFit fit;
    fit.alpha = 0.2;
    fit.beta = 0.5;
    fit.rho = -0.2;
    fit.nu = 0.6;
    QuoteContext ctx;
    ctx.def = &def;
    ctx.ticker = &ticker;
    ctx.forward = 100.0;
    ctx.t_years = 0.5;
    ctx.mark_iv = 0.8;
    ctx.fit = &fit;

    auto fixed = make_strategy("fixed_spread", {{"half_spread_vp", 2.0}});
    auto follower = make_strategy("mark_iv_follower", {{"half_spread_vp", 2.0}});
    const Quote fixed_quote = fixed->quote(ctx);
    const Quote follower_quote = follower->quote(ctx);
    const double fixed_centre = ctx.sabr_iv();
    REQUIRE(fixed_quote.bid.has_value());
    REQUIRE(fixed_quote.ask.has_value());
    REQUIRE(follower_quote.bid.has_value());
    REQUIRE(follower_quote.ask.has_value());
    CHECK(*fixed_quote.bid == doctest::Approx(ctx.price_btc(fixed_centre - 0.02)));
    CHECK(*fixed_quote.ask == doctest::Approx(ctx.price_btc(fixed_centre + 0.02)));
    CHECK(*follower_quote.bid == doctest::Approx(ctx.price_btc(0.78)));
    CHECK(*follower_quote.ask == doctest::Approx(ctx.price_btc(0.82)));
    CHECK(*fixed_quote.bid != doctest::Approx(*follower_quote.bid));

    ctx.fit = nullptr;
    CHECK_FALSE(fixed->quote(ctx).bid.has_value());
    CHECK(follower->quote(ctx).bid.has_value());
}

TEST_CASE("M4 a late print fills an old order inside its lifetime, never its replacement") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    cfg.late_window = kSec;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);

    InstrumentDef def;
    def.id = 0;
    def.name = "BTC-TEST-100-C";
    def.kind = InstrumentKind::Option;
    def.is_call = true;
    def.strike = 100.0;
    def.expiry = 100 * kSec;
    def.min_trade_amount = 0.1;
    def.settlement_period = "day";
    loop.process({kSec, kSec, def});
    BookUpdate book;
    book.id = 0;
    book.snapshot = true;
    book.change_id = 1;
    book.bids = {{BookAction::New, 100, 1.0}};
    book.asks = {{BookAction::New, 120, 1.0}};
    loop.process({2 * kSec, 2 * kSec, book});

    OrderRequest place;
    place.account = 0;
    place.client_id = 1;
    place.instrument = 0;
    place.side = Side::Buy;
    place.price = 100;
    place.amount = 0.5;
    loop.submit_order(place);
    loop.process({3 * kSec, 3 * kSec, IndexPrice{100.0}});  // place arrives at 2 s

    OrderRequest replace = place;
    replace.action = OrderAction::Replace;
    replace.client_id = 1;
    replace.new_id = 2;
    loop.submit_order(replace);
    loop.process({4 * kSec, 4 * kSec, IndexPrice{100.0}});  // replacement arrives at 3 s

    loop.process({kSec, 4 * kSec, Trade{0, 100, 0.2, false}});  // before either order
    CHECK(sim.account(0).fills().empty());
    loop.process({2 * kSec + 500 * kMs, 4 * kSec + 1, Trade{0, 100, 0.2, false}});
    REQUIRE(sim.account(0).fills().size() == 1);
    CHECK(sim.account(0).fills()[0].client_id == 1);
    CHECK(sim.account(0).fills()[0].time == 2 * kSec + 500 * kMs);
    CHECK(sim.account(0).fills()[0].amount == doctest::Approx(0.2));
    CHECK(sim.account(0).position(0) == doctest::Approx(0.2));

    const auto& resting = sim.resting(0);
    REQUIRE(resting.size() == 2);
    CHECK(resting[0].remaining == doctest::Approx(0.3));
    CHECK(resting[1].remaining == doctest::Approx(0.5));
}

TEST_CASE("M4 order latency delays activation and cancellation") {
    EventLoop loop({0, 10 * kMs});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    InstrumentDef def;
    def.id = 0;
    def.name = "BTC-LATENCY-100-C";
    def.kind = InstrumentKind::Option;
    def.strike = 100;
    def.expiry = 100 * kSec;
    def.min_trade_amount = 0.1;
    loop.process({kSec, kSec, def});
    BookUpdate book;
    book.id = 0;
    book.snapshot = true;
    book.change_id = 1;
    book.bids = {{BookAction::New, 100, 1.0}};
    book.asks = {{BookAction::New, 120, 1.0}};
    loop.process({2 * kSec, 2 * kSec, book});

    OrderRequest place;
    place.account = 0;
    place.client_id = 1;
    place.instrument = 0;
    place.side = Side::Buy;
    place.price = 100;
    place.amount = 0.5;
    loop.submit_order(place);
    loop.process({2 * kSec + 5 * kMs, 0, Trade{0, 100, 0.2, false}});
    CHECK(sim.account(0).fills().empty());  // trade preceded the 10 ms arrival
    loop.process({2 * kSec + 10 * kMs, 0, IndexPrice{100.0}});
    REQUIRE(sim.resting(0).size() == 1);
    CHECK(sim.resting(0)[0].active_from == 2 * kSec + 10 * kMs);

    OrderRequest cancel = place;
    cancel.action = OrderAction::Cancel;
    loop.submit_order(cancel);
    loop.process({2 * kSec + 15 * kMs, 0, Trade{0, 100, 0.2, false}});
    REQUIRE(sim.account(0).fills().size() == 1);  // cancel has not arrived yet
    CHECK(sim.account(0).fills()[0].amount == doctest::Approx(0.2));
    loop.process({2 * kSec + 20 * kMs, 0, IndexPrice{100.0}});
    CHECK(sim.resting(0)[0].dead_at == 2 * kSec + 20 * kMs);
    CHECK(sim.resting(0)[0].remaining == doctest::Approx(0.3));
}

TEST_CASE("M4 expiry settles on the final delivery ticker or the estimate timeout") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    fees.option_maker = 0.0003;
    fees.option_cap = 0.125;
    fees.delivery["day"] = {0.0, 0.125};
    fees.delivery["week"] = {0.00015, 0.125};
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    cfg.settle_wait = kSec;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);

    InstrumentDef call;
    call.id = 0;
    call.name = "BTC-SETTLE-100-C";
    call.kind = InstrumentKind::Option;
    call.is_call = true;
    call.strike = 100;
    call.expiry = 5 * kSec;
    call.min_trade_amount = 0.1;
    call.settlement_period = "day";
    InstrumentDef put = call;
    put.id = 1;
    put.name = "BTC-SETTLE-110-P";
    put.is_call = false;
    put.strike = 110;
    put.settlement_period = "week";
    loop.process({kSec, kSec, call});
    loop.process({kSec, kSec, put});

    for (InstrumentId id : {0, 1}) {
        BookUpdate book;
        book.id = id;
        book.snapshot = true;
        book.change_id = 1;
        book.bids = {{BookAction::New, 1'000'000, 1.0}};
        book.asks = {{BookAction::New, 1'200'000, 1.0}};
        loop.process({2 * kSec, 2 * kSec, book});
        OrderRequest order;
        order.account = 0;
        order.client_id = static_cast<std::uint64_t>(id + 1);
        order.instrument = id;
        order.side = Side::Buy;
        order.price = 1'000'000;
        order.amount = 0.1;
        loop.submit_order(order);
    }
    loop.process({3 * kSec, 3 * kSec, Trade{0, 1'000'000, 0.1, false}});
    loop.process({3 * kSec, 3 * kSec, Trade{1, 1'000'000, 0.1, false}});
    for (InstrumentId id : {0, 1}) {
        Ticker estimate;
        estimate.id = id;
        estimate.est_delivery_price = 100;
        loop.process({4 * kSec, 4 * kSec, estimate});
    }
    loop.process({5 * kSec, 5 * kSec, IndexPrice{100}});  // expiry; freeze at estimates

    Ticker delivery;
    delivery.id = 0;
    delivery.delivery_price = 110;
    loop.process({5 * kSec + 500 * kMs, 5 * kSec + 500 * kMs, delivery});
    loop.process({6 * kSec, 6 * kSec, IndexPrice{100}});  // timeout for instrument 1

    CHECK(sim.settled_final() == 1);
    CHECK(sim.settled_estimate() == 1);
    CHECK(sim.account(0).position(0) == 0.0);
    CHECK(sim.account(0).position(1) == 0.0);
    CHECK(sim.stats(0).settlements == 2);
    CHECK(sim.stats(0).delivery_fees == doctest::Approx(0.000015));
    CHECK(sim.account(0).equity() - sim.account(0).starting_equity() ==
          doctest::Approx(sim.account(0).pnl().total()).epsilon(1e-12));
}

TEST_CASE("M4 inverse perpetual IOC uses BTC unit value and taker fee") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    fees.perp_taker = 0.00035;
    ExchangeSim sim(loop, fees, SimConfig{}, {"test"});
    loop.add_exchange_observer(&sim);
    InstrumentDef perp;
    perp.id = 0;
    perp.name = "BTC-PERPETUAL";
    perp.kind = InstrumentKind::Perpetual;
    loop.process({kSec, kSec, perp});
    BookUpdate book;
    book.id = 0;
    book.snapshot = true;
    book.change_id = 1;
    book.bids = {{BookAction::New, 80'000 * 100'000'000LL, 100.0}};
    book.asks = {{BookAction::New, 80'010 * 100'000'000LL, 100.0}};
    loop.process({2 * kSec, 2 * kSec, book});

    OrderRequest ioc;
    ioc.account = 0;
    ioc.client_id = 1;
    ioc.action = OrderAction::Place;
    ioc.type = OrderType::Ioc;
    ioc.instrument = 0;
    ioc.side = Side::Buy;
    ioc.price = 80'010 * 100'000'000LL;
    ioc.amount = 100.0;
    loop.submit_order(ioc);
    loop.process({3 * kSec, 3 * kSec, IndexPrice{80'000.0}});

    REQUIRE(sim.account(0).fills().size() == 1);
    const FillRecord& fill = sim.account(0).fills()[0];
    CHECK_FALSE(fill.option);
    CHECK_FALSE(fill.maker);
    CHECK(fill.u_fill == doctest::Approx(-1.0 / 80'010.0));
    CHECK(fill.u_mid == doctest::Approx(-1.0 / 80'005.0));
    CHECK(fill.fee == doctest::Approx(0.00035 * 100.0 / 80'010.0));
    CHECK(sim.account(0).equity() - sim.account(0).starting_equity() ==
          doctest::Approx(sim.account(0).pnl().total()).epsilon(1e-12));
}

TEST_CASE("M4 late maker fills use the mid known at their trade timestamp") {
    auto start = [](EventLoop& loop, ExchangeSim& sim) {
        loop.add_exchange_observer(&sim);
        seed_option(loop);
        OrderRequest p;
        p.account = 0;
        p.client_id = 1;
        p.instrument = 0;
        p.side = Side::Buy;
        p.price = 100;
        p.amount = 0.5;
        loop.submit_order(p);
        pulse(loop, 3 * kSec);
        return p;
    };

    SUBCASE("original repro: late print does not take the arrival-time book mid") {
        EventLoop loop({0, 0});
        FeeSchedule fees;
        SimConfig cfg;
        cfg.fill_model = FillModelKind::Optimistic;
        ExchangeSim sim(loop, fees, cfg, {"test"});
        start(loop, sim);
        BookUpdate move;
        move.id = 0;
        move.change_id = 2;
        move.prev_change_id = 1;
        move.bids = {{BookAction::Change, 100, 1.0}};
        move.asks = {{BookAction::Change, 120, 1.0}};
        // Keep the original mid for the order/trade period, then move the current book.
        move.bids[0].price = 200;
        move.asks[0].price = 240;
        loop.process({4 * kSec, 4 * kSec, move});
        loop.process({3500 * kMs, 4 * kSec + 1, Trade{0, 100, 0.1, false}});
        REQUIRE(sim.account(0).fills().size() == 1);
        CHECK(std::abs(sim.account(0).fills()[0].mid - 1.1e-6) < 1e-12);
        CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.1e-6) < 1e-12);
    }

    SUBCASE("print older than retained history uses earliest known value") {
        EventLoop loop({0, 0});
        FeeSchedule fees;
        SimConfig cfg;
        cfg.fill_model = FillModelKind::Optimistic;
        cfg.late_window = 100 * kMs;
        ExchangeSim sim(loop, fees, cfg, {"test"});
        start(loop, sim);
        for (int n = 2; n <= 4; ++n) {
            BookUpdate move;
            move.id = 0;
            move.change_id = n;
            move.prev_change_id = n - 1;
            move.bids = {{BookAction::Change, 200 + n, 1.0}};
            move.asks = {{BookAction::Change, 240 + n, 1.0}};
            loop.process({(3 + n / 2) * kSec, (3 + n / 2) * kSec, move});
        }
        // The trade is eligible by its exchange timestamp but predates all retained mids.
        loop.process({3500 * kMs, 5 * kSec + 1, Trade{0, 100, 0.1, false}});
        REQUIRE(sim.account(0).fills().size() == 1);
        CHECK(std::abs(sim.account(0).fills()[0].u_mid - (from_e8(203 + 120)) / 2.0) < 1e-12);
    }

    SUBCASE("one-sided book records the ticker mark fallback") {
        EventLoop loop({0, 0});
        FeeSchedule fees;
        SimConfig cfg;
        cfg.fill_model = FillModelKind::Optimistic;
        ExchangeSim sim(loop, fees, cfg, {"test"});
        start(loop, sim);
        BookUpdate one_side;
        one_side.id = 0;
        one_side.change_id = 2;
        one_side.prev_change_id = 1;
        one_side.asks = {{BookAction::Delete, 120, 0.0}};
        loop.process({3100 * kMs, 3100 * kMs, one_side});
        Ticker mark;
        mark.id = 0;
        mark.mark_price = 1.7e-6;
        mark.mark_iv = 80;
        mark.underlying_price = 100;
        loop.process({3200 * kMs, 3200 * kMs, mark});
        BookUpdate restore;
        restore.id = 0;
        restore.change_id = 3;
        restore.prev_change_id = 2;
        restore.asks = {{BookAction::New, 240, 1.0}};
        loop.process({4 * kSec, 4 * kSec, restore});
        loop.process({3500 * kMs, 4 * kSec + 1, Trade{0, 100, 0.1, false}});
        REQUIRE(sim.account(0).fills().size() == 1);
        CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.7e-6) < 1e-12);
    }

    SUBCASE("late print fills a cancelled order during its late window") {
        EventLoop loop({0, 0});
        FeeSchedule fees;
        SimConfig cfg;
        cfg.fill_model = FillModelKind::Optimistic;
        cfg.late_window = kSec;
        ExchangeSim sim(loop, fees, cfg, {"test"});
        const auto p = start(loop, sim);
        OrderRequest c = p;
        c.action = OrderAction::Cancel;
        pulse(loop, 4 * kSec);
        loop.submit_order(c);
        pulse(loop, 5 * kSec);
        REQUIRE(sim.resting(0).size() == 1);
        CHECK(sim.resting(0)[0].dead_at == 4 * kSec);
        loop.process({3500 * kMs, 5 * kSec + 1, Trade{0, 100, 0.1, false}});
        REQUIRE(sim.account(0).fills().size() == 1);
        CHECK(sim.account(0).fills()[0].client_id == 1);
        CHECK(std::abs(sim.account(0).fills()[0].mid - 1.1e-6) < 1e-12);
    }
}

TEST_CASE("M4 out-of-order mid events retain their own timestamp") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);

    // A book gap at 4 s does not affect the valid book mid at 3.75 s. A delayed ticker stamped
    // 3.5 s then supplies the fallback for a print after the gap.
    BookUpdate gap;
    gap.id = 0;
    gap.change_id = 10;
    gap.prev_change_id = 9;
    loop.process({4 * kSec, 4 * kSec, gap});
    Ticker older_mark;
    older_mark.id = 0;
    older_mark.mark_price = 0.00012;
    older_mark.mark_iv = 80;
    older_mark.underlying_price = 100;
    loop.process({3500 * kMs, 4100 * kMs, older_mark});
    loop.process({3750 * kMs, 4200 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({4500 * kMs, 4500 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 2);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.1e-6) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - 0.00012) < 1e-12);
}

TEST_CASE("M4 a late book change wins over an earlier processed ticker by event time") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);

    Ticker ticker;
    ticker.id = 0;
    ticker.mark_price = 0.00012;
    ticker.mark_iv = 80;
    ticker.underlying_price = 100;
    loop.process({3200 * kMs, 3200 * kMs, ticker});
    BookUpdate late_book;
    late_book.id = 0;
    late_book.change_id = 2;
    late_book.prev_change_id = 1;
    late_book.bids = {{BookAction::Change, 200, 1.0}};
    loop.process({3100 * kMs, 3300 * kMs, late_book});
    loop.process({3150 * kMs, 3400 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({3300 * kMs, 3500 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 2);
    const double changed_book_mid = from_e8(200 + 120) / 2.0;
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - changed_book_mid) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - changed_book_mid) < 1e-12);
}

TEST_CASE("M4 ticker stamps select marks while the book is invalid") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);

    BookUpdate gap;
    gap.id = 0;
    gap.change_id = 10;
    gap.prev_change_id = 9;
    loop.process({4 * kSec, 4 * kSec, gap});
    pulse(loop, 5 * kSec);
    Ticker late_mark;
    late_mark.id = 0;
    late_mark.mark_price = 0.00012;
    late_mark.mark_iv = 80;
    late_mark.underlying_price = 100;
    loop.process({4250 * kMs, 5100 * kMs, late_mark});
    loop.process({4100 * kMs, 5200 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({4500 * kMs, 5300 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 2);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - 0.00011) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - 0.00012) < 1e-12);
}

TEST_CASE("M4 disconnect invalidates the book history at disconnect time") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);
    pulse(loop, 4 * kSec);
    loop.process({0, 4 * kSec, RecorderEvent{RecorderEvent::Kind::Disconnect}});
    loop.process({3500 * kMs, 4100 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({4500 * kMs, 4200 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 2);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.1e-6) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - 0.00011) < 1e-12);
}

TEST_CASE("M4 late ticker replaces newer-stamped mark history from its own timestamp") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);
    BookUpdate gap;
    gap.id = 0;
    gap.change_id = 10;
    gap.prev_change_id = 9;
    loop.process({4 * kSec, 4 * kSec, gap});
    Ticker newer;
    newer.id = 0;
    newer.mark_price = 0.00013;
    newer.mark_iv = 80;
    newer.underlying_price = 100;
    loop.process({4250 * kMs, 4250 * kMs, newer});
    Ticker older = newer;
    older.mark_price = 0.00012;
    loop.process({3500 * kMs, 4500 * kMs, older});
    loop.process({3750 * kMs, 4600 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({4500 * kMs, 4700 * kMs, Trade{0, 100, 0.1, false}});

    // At 3.75 s the book is still valid: the invalidating gap is stamped 4 s. SPEC §6
    // therefore selects the book there. Once the book is invalid, the late-arriving
    // 3.5 s ticker has replaced the 4.25 s mark and supplies 0.00012.
    REQUIRE(sim.account(0).fills().size() == 2);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.1e-6) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - 0.00012) < 1e-12);
}

TEST_CASE("M4 late same-channel book message replaces entries from its own timestamp") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);

    BookUpdate first;
    first.id = 0;
    first.change_id = 2;
    first.prev_change_id = 1;
    first.bids = {{BookAction::Change, 200, 1.0}};
    loop.process({4 * kSec, 4 * kSec, first});
    BookUpdate late = first;
    late.change_id = 3;
    late.prev_change_id = 2;
    late.bids = {{BookAction::Change, 300, 1.0}};
    loop.process({3500 * kMs, 4500 * kMs, late});
    loop.process({3250 * kMs, 4600 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({3750 * kMs, 4700 * kMs, Trade{0, 100, 0.1, false}});
    loop.process({4500 * kMs, 4800 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 3);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.1e-6) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - from_e8(300 + 120) / 2.0) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[2].u_mid - from_e8(300 + 120) / 2.0) < 1e-12);
}

TEST_CASE("M4 late message before retained history supplies its value before its own stamp") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    cfg.late_window = 100 * kMs;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);

    BookUpdate update;
    update.id = 0;
    update.change_id = 2;
    update.prev_change_id = 1;
    update.bids = {{BookAction::Change, 200, 1.0}};
    loop.process({4900 * kMs, 4900 * kMs, update});
    update.change_id = 3;
    update.prev_change_id = 2;
    update.bids = {{BookAction::Change, 300, 1.0}};
    loop.process({5 * kSec, 5 * kSec, update});
    // The 100 ms retention window leaves only the 4.9 s and 5 s book states.
    // This 3.5 s arrival drops both; a 3.25 s print asks before that remaining history.
    update.change_id = 4;
    update.prev_change_id = 3;
    update.bids = {{BookAction::Change, 400, 1.0}};
    loop.process({3500 * kMs, 5100 * kMs, update});
    loop.process({3250 * kMs, 5200 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 1);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - from_e8(400 + 120) / 2.0) < 1e-12);
}

TEST_CASE("M4 late same-value book message leaves historical lookups unchanged") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::Optimistic;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);

    BookUpdate update;
    update.id = 0;
    update.change_id = 2;
    update.prev_change_id = 1;
    update.bids = {{BookAction::Change, 200, 1.0}};
    loop.process({4 * kSec, 4 * kSec, update});
    loop.process({4500 * kMs, 4500 * kMs, Trade{0, 100, 0.1, false}});
    update.change_id = 3;
    update.prev_change_id = 2;
    loop.process({4250 * kMs, 5 * kSec, update});
    loop.process({4750 * kMs, 5100 * kMs, Trade{0, 100, 0.1, false}});

    REQUIRE(sim.account(0).fills().size() == 2);
    CHECK(std::abs(sim.account(0).fills()[0].u_mid - 1.6e-6) < 1e-12);
    CHECK(std::abs(sim.account(0).fills()[1].u_mid - 1.6e-6) < 1e-12);
}

TEST_CASE("M4 received fits respect compute time and only add positive latency differences") {
    for (const auto [md_ms, expected_ms] : {std::pair{10, 1005}, std::pair{100, 1068}}) {
        EventLoop loop({md_ms * kMs, 0, MdTiming::AsReceived, 37 * kMs});
        TimeProbe probe;
        loop.add_market_observer(&probe);
        SabrFit fit;
        fit.fit_time = 1000 * kMs;
        fit.compute_ns = 5 * kMs;
        loop.process({fit.fit_time + fit.compute_ns, 0, fit});
        loop.process({(expected_ms - 1) * kMs, 0, IndexPrice{1}});
        CHECK(probe.times.empty());
        loop.process({expected_ms * kMs, 0, IndexPrice{1}});
        REQUIRE(probe.times.size() == 1);
        CHECK(probe.times[0] == expected_ms * kMs);
    }
}

TEST_CASE("M4 quoter rounds by tick steps and clamps crossing quotes inside the book") {
    auto orders_for = [](Quote quote) {
        EventLoop loop({0, 0});
        CaptureExchange capture;
        auto strategy = std::make_unique<TestStrategy>();
        strategy->next = quote;
        QuoterConfig cfg;
        cfg.requote_interval = 0;
        cfg.stale_after = 60 * kSec;
        cfg.no_quote_before_expiry = 0;
        cfg.reduce_only_before_expiry = 0;
        Quoter quoter(loop, 0, std::move(strategy), cfg);
        loop.add_market_observer(&quoter);
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
        pulse(loop, 3 * kSec);
        return capture.orders;
    };

    auto boundary = orders_for({0.020, 0.020001, std::nullopt});
    REQUIRE(boundary.size() == 2);
    CHECK(boundary[0].price == to_e8(0.020));  // exact threshold still uses base tick
    CHECK(boundary[1].price == to_e8(0.025));  // just above threshold uses the larger step

    auto clamp_bid = orders_for({0.03, std::nullopt, std::nullopt});
    REQUIRE(clamp_bid.size() == 1);
    CHECK(clamp_bid[0].side == Side::Buy);
    CHECK(clamp_bid[0].price == to_e8(0.025));  // best ask minus its active tick

    auto clamp_ask = orders_for({std::nullopt, 0.01, std::nullopt});
    REQUIRE(clamp_ask.size() == 1);
    CHECK(clamp_ask[0].side == Side::Sell);
    CHECK(clamp_ask[0].price == to_e8(0.011));  // best bid plus the active base tick
}

TEST_CASE("M4 quoter risk, stale pull, and slot races reconcile from exchange updates") {
    EventLoop loop({0, 0});
    CaptureExchange capture;
    auto strategy = std::make_unique<TestStrategy>();
    TestStrategy* strategy_ptr = strategy.get();
    strategy_ptr->next = {0.02, 0.025, std::nullopt};
    QuoterConfig cfg;
    cfg.size = 1.0;
    cfg.max_position = 1.0;
    cfg.max_vega = 1e9;
    cfg.requote_interval = 0;
    cfg.stale_after = kSec;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 0;
    Quoter quoter(loop, 0, std::move(strategy), cfg);
    loop.add_market_observer(&quoter);
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
    pulse(loop, 3 * kSec);
    REQUIRE(capture.orders.size() == 2);
    const auto bid_id = capture.orders[0].client_id;
    const auto ask_id = capture.orders[1].client_id;

    auto update = [&](std::uint64_t id, UpdateKind kind, Side side, double amount,
                      double remaining, Px price) {
        OrderUpdate u;
        u.account = 0;
        u.instrument = 0;
        u.client_id = id;
        u.kind = kind;
        u.side = side;
        u.amount = amount;
        u.remaining = remaining;
        u.price = price;
        quoter.on_order_update(u, loop.view(), loop.now());
    };
    update(bid_id, UpdateKind::Accepted, Side::Buy, 0, 1, to_e8(0.02));
    update(ask_id, UpdateKind::Accepted, Side::Sell, 0, 1, to_e8(0.025));
    update(bid_id, UpdateKind::Fill, Side::Buy, 1, 0, to_e8(0.02));
    CHECK(quoter.position(0) == doctest::Approx(1.0));

    // Fresh ticker re-evaluation at the long position limit must not add another bid.
    k.mark_price = 0.021;
    loop.process({4 * kSec, 4 * kSec, k});
    pulse(loop, 5 * kSec);
    CHECK(capture.orders.size() == 2);

    // The old book/ticker are now stale; periodic sweep cancels the still-live ask.
    pulse(loop, 14 * kSec);
    pulse(loop, 15 * kSec);
    REQUIRE(capture.orders.size() == 3);
    CHECK(capture.orders.back().action == OrderAction::Cancel);
    CHECK(capture.orders.back().client_id == ask_id);
    update(ask_id, UpdateKind::Cancelled, Side::Sell, 0, 0, to_e8(0.025));
}

TEST_CASE("M4 quoter stops adding same-direction risk at the account vega limit") {
    EventLoop loop({0, 0});
    CaptureExchange capture;
    auto strategy = std::make_unique<TestStrategy>();
    strategy->next = {0.02, 0.025, std::nullopt};
    QuoterConfig cfg;
    cfg.size = 1.0;
    cfg.max_position = 10.0;
    cfg.max_vega = 1e-6;
    cfg.requote_interval = 0;
    cfg.stale_after = 60 * kSec;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 0;
    Quoter quoter(loop, 0, std::move(strategy), cfg);
    loop.add_market_observer(&quoter);
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
    pulse(loop, 3 * kSec);
    REQUIRE(capture.orders.size() == 2);

    auto update = [&](const OrderRequest& o, UpdateKind kind, double amount, double remaining) {
        OrderUpdate u;
        u.account = 0;
        u.instrument = 0;
        u.client_id = o.client_id;
        u.kind = kind;
        u.side = o.side;
        u.amount = amount;
        u.remaining = remaining;
        u.price = o.price;
        quoter.on_order_update(u, loop.view(), loop.now());
    };
    update(capture.orders[0], UpdateKind::Accepted, 0, 1);
    update(capture.orders[1], UpdateKind::Accepted, 0, 1);
    update(capture.orders[0], UpdateKind::Fill, 1, 0);
    CHECK(quoter.account_vega() > cfg.max_vega);
    k.mark_price = 0.021;
    loop.process({4 * kSec, 4 * kSec, k});
    pulse(loop, 5 * kSec);
    CHECK(capture.orders.size() == 2);  // no new bid; the existing ask remains
}

TEST_CASE("M4 quoter slot state survives fill, replace, and cancel races") {
    EventLoop loop({0, 0});
    CaptureExchange capture;
    auto strategy = std::make_unique<TestStrategy>();
    TestStrategy* strategy_ptr = strategy.get();
    strategy_ptr->next = {0.02, 0.025, std::nullopt};
    QuoterConfig cfg;
    cfg.requote_interval = 0;
    cfg.stale_after = 60 * kSec;
    cfg.no_quote_before_expiry = 0;
    cfg.reduce_only_before_expiry = 0;
    Quoter quoter(loop, 0, std::move(strategy), cfg);
    loop.add_market_observer(&quoter);
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
    pulse(loop, 3 * kSec);
    REQUIRE(capture.orders.size() == 2);
    const auto old_bid = capture.orders[0].client_id;
    const auto ask_id = capture.orders[1].client_id;

    auto update = [&](std::uint64_t id, UpdateKind kind, Side side, double amount,
                      double remaining, Px price) {
        OrderUpdate u;
        u.account = 0;
        u.instrument = 0;
        u.client_id = id;
        u.kind = kind;
        u.side = side;
        u.amount = amount;
        u.remaining = remaining;
        u.price = price;
        quoter.on_order_update(u, loop.view(), loop.now());
    };
    update(old_bid, UpdateKind::Accepted, Side::Buy, 0, 1, to_e8(0.02));
    update(ask_id, UpdateKind::Accepted, Side::Sell, 0, 1, to_e8(0.025));

    strategy_ptr->next.bid = 0.019;
    loop.process({4 * kSec, 4 * kSec, k});
    pulse(loop, 5 * kSec);
    REQUIRE(capture.orders.size() == 3);
    CHECK(capture.orders.back().action == OrderAction::Replace);
    CHECK(capture.orders.back().client_id == old_bid);
    const auto replacement = capture.orders.back().new_id;

    update(old_bid, UpdateKind::Fill, Side::Buy, 0.2, 0.8, to_e8(0.02));
    CHECK(quoter.position(0) == doctest::Approx(0.2));
    update(replacement, UpdateKind::Accepted, Side::Buy, 0, 1, to_e8(0.019));
    strategy_ptr->next.bid = 0.018;
    loop.process({6 * kSec, 6 * kSec, k});
    pulse(loop, 7 * kSec);
    REQUIRE(capture.orders.size() == 4);
    CHECK(capture.orders.back().action == OrderAction::Replace);
    CHECK(capture.orders.back().client_id == replacement);
    const auto second_replacement = capture.orders.back().new_id;
    update(second_replacement, UpdateKind::Accepted, Side::Buy, 0, 1, to_e8(0.018));

    strategy_ptr->next.bid.reset();
    loop.process({8 * kSec, 8 * kSec, k});
    pulse(loop, 9 * kSec);
    REQUIRE(capture.orders.size() == 5);
    CHECK(capture.orders.back().action == OrderAction::Cancel);
    CHECK(capture.orders.back().client_id == second_replacement);
    update(second_replacement, UpdateKind::Fill, Side::Buy, 0.3, 0.7, to_e8(0.018));
    update(second_replacement, UpdateKind::Cancelled, Side::Buy, 0, 0, to_e8(0.018));
    CHECK(quoter.position(0) == doctest::Approx(0.5));

    strategy_ptr->next.bid = 0.017;
    loop.process({10 * kSec, 10 * kSec, k});
    pulse(loop, 11 * kSec);
    REQUIRE(capture.orders.size() == 6);
    CHECK(capture.orders.back().action == OrderAction::Place);
    CHECK(capture.orders.back().client_id != second_replacement);
}

TEST_CASE("M4 queue position resets to displayed size after a book snapshot") {
    EventLoop loop({0, 0});
    FeeSchedule fees;
    SimConfig cfg;
    cfg.fill_model = FillModelKind::QueueAware;
    ExchangeSim sim(loop, fees, cfg, {"test"});
    loop.add_exchange_observer(&sim);
    seed_option(loop);
    OrderRequest p;
    p.account = 0;
    p.client_id = 1;
    p.instrument = 0;
    p.side = Side::Buy;
    p.price = 100;
    p.amount = 0.5;
    loop.submit_order(p);
    pulse(loop, 3 * kSec);
    REQUIRE(sim.resting(0).size() == 1);
    CHECK(sim.resting(0)[0].queue_ahead == doctest::Approx(1.0));

    BookUpdate delete_level;
    delete_level.id = 0;
    delete_level.change_id = 2;
    delete_level.prev_change_id = 1;
    delete_level.bids = {{BookAction::Delete, 100, 0.0}};
    loop.process({4 * kSec, 4 * kSec, delete_level});
    CHECK(sim.resting(0)[0].queue_ahead == doctest::Approx(0.0));

    BookUpdate snapshot;
    snapshot.id = 0;
    snapshot.snapshot = true;
    snapshot.change_id = 9;
    snapshot.bids = {{BookAction::New, 100, 1.0}};
    snapshot.asks = {{BookAction::New, 120, 1.0}};
    loop.process({5 * kSec, 5 * kSec, snapshot});
    CHECK(sim.resting(0)[0].queue_ahead == doctest::Approx(1.0));
    loop.process({5500 * kMs, 5500 * kMs, Trade{0, 100, 0.5, false}});
    CHECK(sim.account(0).fills().empty());
    CHECK(sim.resting(0)[0].remaining == doctest::Approx(0.5));
}
