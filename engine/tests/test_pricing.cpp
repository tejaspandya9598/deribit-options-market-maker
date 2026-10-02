#include <doctest.h>

#include "pricing.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace mm::pricing;

TEST_CASE("Black-76 prices and Greeks match independent reference values") {
    struct Ref { double F, K, T, vol, call[5], put[5]; };
    // Values calculated independently with Python's math.erf normal CDF.
    const std::array refs{
        Ref{100, 80, .25, .1,
            {20.000003719833188, .9999964030772182, 3.375829048115078e-6,
             .0008439572620287696, -.00016879145240575392},
            {3.7198331813065094e-6, -3.596922781845535e-6, 3.375829048115078e-6,
             .0008439572620287696, -.00016879145240575392}},
        Ref{100, 100, 1, .2,
            {7.965567455405804, .539827837277029, .01984762737385059,
             39.69525474770118, -3.969525474770118},
            {7.965567455405804, -.460172162722971, .01984762737385059,
             39.69525474770118, -3.969525474770118}},
        Ref{100, 120, 2, .5,
            {21.35623909030562, .5381252545428925, .005616112948280356,
             56.16112948280357, -7.020141185350445},
            {41.356239090305635, -.4618747454571075, .005616112948280356,
             56.16112948280357, -7.020141185350445}},
    };
    for (const auto& r : refs) {
        for (bool call : {true, false}) {
            const auto g = black76(r.F, r.K, r.T, r.vol, call);
            const auto& want = call ? r.call : r.put;
            CHECK(g.price == doctest::Approx(want[0]).epsilon(1e-8));
            CHECK(g.delta == doctest::Approx(want[1]).epsilon(1e-8));
            CHECK(g.gamma == doctest::Approx(want[2]).epsilon(1e-8));
            CHECK(g.vega == doctest::Approx(want[3]).epsilon(1e-8));
            CHECK(g.theta == doctest::Approx(want[4]).epsilon(1e-8));
        }
    }
}

TEST_CASE("Black-76 put-call parity and Greeks agree with finite differences") {
    for (double F : {70.0, 100.0, 150.0}) for (double K : {80.0, 100.0, 120.0})
        for (double T : {.1, 1.0, 3.0}) for (double vol : {.05, .3, 1.2}) {
            const auto c = black76(F, K, T, vol, true);
            const auto p = black76(F, K, T, vol, false);
            CHECK(c.price - p.price == doctest::Approx(F - K).epsilon(1e-12));

            const double hf = F * 1e-4;
            const double hp = black76_price(F + hf, K, T, vol, true);
            const double hm = black76_price(F - hf, K, T, vol, true);
            CHECK((hp - hm) / (2 * hf) == doctest::Approx(c.delta).epsilon(2e-7));
            CHECK((hp - 2 * c.price + hm) / (hf * hf) == doctest::Approx(c.gamma).epsilon(2e-5));
            const double hv = vol * 1e-4;
            CHECK((black76_price(F, K, T, vol + hv, true) -
                   black76_price(F, K, T, vol - hv, true)) / (2 * hv)
                  == doctest::Approx(c.vega).epsilon(2e-7));
            const double ht = T * 1e-4;
            CHECK(-(black76_price(F, K, T + ht, vol, true) -
                    black76_price(F, K, T - ht, vol, true)) / (2 * ht)
                  == doctest::Approx(c.theta).epsilon(2e-7));
        }
}

TEST_CASE("implied volatility round trips price over strike, expiry and volatility grid") {
    for (double F : {100.0, 77000.0}) for (double m : {.95, .98, 1.0, 1.02, 1.05})
        for (double T : {.05, .25, 1.0, 5.0}) for (double vol : {.1, .2, .4, .8})
            for (bool call : {true, false}) {
                const double K = F * m;
                const double px = black76_price(F, K, T, vol, call);
                const auto iv = implied_vol(px, F, K, T, call);
                INFO("F=" << F << " K=" << K << " T=" << T << " vol=" << vol);
                REQUIRE(iv.status == IvStatus::Ok);
                CHECK(iv.iterations <= 20);
                const double repriced = black76_price(F, K, T, iv.vol, call);
                CHECK(std::abs(repriced - px) <= 1e-10);
                CHECK(std::isfinite(iv.vol));
            }
}

TEST_CASE("implied volatility reports unsolvable deep ITM and OTM inputs") {
    for (bool call : {true, false}) {
        const double F = call ? 1000.0 : 100.0;
        const double K = call ? 100.0 : 1000.0;
        const double intrinsic = 900.0;
        CHECK(implied_vol(intrinsic, F, K, 1, call).status == IvStatus::BelowIntrinsic);
        CHECK(implied_vol(call ? F : K, F, K, 1, call).status == IvStatus::AboveMax);
        const auto tiny = implied_vol(intrinsic + 1e-12, F, K, .01, call);
        CHECK(tiny.status == IvStatus::TooSmall);
        CHECK(std::isfinite(tiny.vol));
        CHECK(std::isfinite(tiny.iterations));
    }
    // The opposite tail has near-zero intrinsic value; its positive price is below
    // double resolution at this scale and must get an explicit status, not a NaN IV.
    for (bool call : {true, false}) {
        const double F = call ? 100.0 : 1000.0;
        const double K = call ? 1000.0 : 100.0;
        const auto tiny_otm = implied_vol(1e-12, F, K, .01, call);
        CHECK(tiny_otm.status == IvStatus::TooSmall);
        CHECK(std::isfinite(tiny_otm.vol));
        CHECK(implied_vol(call ? F : K, F, K, 1, call).status == IvStatus::AboveMax);
    }
    for (double px : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity()}) {
        const auto result = implied_vol(px, 100, 100, 1, true);
        CHECK(result.status != IvStatus::Ok);
        CHECK(std::isfinite(result.vol));
    }
}

TEST_CASE("Hagan SABR port matches sabr.model grid, including branch boundaries") {
    struct Ref { double F, K, beta, alpha, rho, nu, vol; };
    // Expected values generated by sabr.model.hagan_lognormal_vol (T=.7).
    const std::array refs{
        Ref{100, 100, .5, .4, .999, .3, .039938054351666669},
        Ref{100, 100.0000000000005, .5, .4, -.999, .3, .039854138351666672},
        Ref{100, 100.0000013, .5, .4, .999, .3, .039938052276695418},
        Ref{100, 100.0000014, .5, .4, .999, .3, .039938048733751258},
        Ref{100, 100.0000013, 1., .4, -.999, .3, .39056469875209893},
        Ref{100, 100.0000014, 1., .4, -.999, .3, .39056469889841422},
        Ref{100, 100.000013, 1., .4, .999, .3, .40734787701164921},
        Ref{100, 100.000014, 1., .4, .999, .3, .40734824359491467},
        Ref{100, 100.000013, 1., .4, -.999, .3, .3905647158709874},
        Ref{100, 100.000014, 1., .4, -.999, .3, .39056467580969756},
        Ref{100, 80, .5, .4, .999, .3, .00945749284524342},
        Ref{100, 120, 1., .4, -.999, .3, .3632383740528814},
        Ref{77000, 75000, .5, 140, .2, 1.4, .56605090499943644},
        Ref{100, 99.99999999, 1., .4, .999999, .3, .40734999791527249},
    };
    for (const auto& r : refs)
        CHECK(hagan_lognormal_vol(r.F, r.K, .7, r.alpha, r.beta, r.rho, r.nu)
              == doctest::Approx(r.vol).epsilon(1e-12));
}

TEST_CASE("Hagan rejects invalid rho, alpha, forward and strike") {
    for (double rho : {-1.0, 1.0, std::numeric_limits<double>::quiet_NaN()})
        CHECK_THROWS_AS(hagan_lognormal_vol(100, 100, 1, .4, .5, rho, .3), std::invalid_argument);
    for (auto values : {std::array{0.0, 100.0, 100.0}, std::array{-0.4, 100.0, 100.0},
                        std::array{.4, 0.0, 100.0}, std::array{.4, -100.0, 100.0},
                        std::array{.4, 100.0, 0.0}, std::array{.4, 100.0, -100.0}})
        CHECK_THROWS_AS(hagan_lognormal_vol(values[1], values[2], 1, values[0], .5, 0, .3),
                        std::invalid_argument);
}
