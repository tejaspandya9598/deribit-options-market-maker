#include "pricing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace mm::pricing {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double norm_cdf(double x) { return 0.5 * std::erfc(-x / std::numbers::sqrt2); }
double norm_pdf(double x) { return std::exp(-0.5 * x * x) / std::sqrt(2.0 * std::numbers::pi); }

double intrinsic(double F, double K, bool call) { return call ? std::max(F - K, 0.0) : std::max(K - F, 0.0); }

}  // namespace

Greeks black76(double F, double K, double T, double sigma, bool call) {
    Greeks g;
    if (!(F > 0) || !(K > 0)) return {kNaN, kNaN, kNaN, kNaN, kNaN};
    if (!(T > 0) || !(sigma > 0)) {
        g.price = intrinsic(F, K, call);
        g.delta = call ? (F > K ? 1.0 : 0.0) : (F < K ? -1.0 : 0.0);
        return g;
    }
    const double sq = sigma * std::sqrt(T);
    const double d1 = (std::log(F / K) + 0.5 * sigma * sigma * T) / sq;
    const double d2 = d1 - sq;
    const double pdf = norm_pdf(d1);
    if (call) {
        g.price = F * norm_cdf(d1) - K * norm_cdf(d2);
        g.delta = norm_cdf(d1);
    } else {
        g.price = K * norm_cdf(-d2) - F * norm_cdf(-d1);
        g.delta = norm_cdf(d1) - 1.0;
    }
    g.gamma = pdf / (F * sq);
    g.vega = F * pdf * std::sqrt(T);
    g.theta = -F * pdf * sigma / (2.0 * std::sqrt(T));
    return g;
}

double black76_price(double F, double K, double T, double sigma, bool call) {
    return black76(F, K, T, sigma, call).price;
}

IvResult implied_vol(double price, double F, double K, double T, bool call) {
    IvResult r;
    if (!std::isfinite(price) || !(price > 0) || !(F > 0) || !(K > 0) || !(T > 0) ||
        !std::isfinite(F) || !std::isfinite(K) || !std::isfinite(T)) {
        return r;  // BadInput
    }
    const double lower = intrinsic(F, K, call);
    const double upper = call ? F : K;
    if (price <= lower) return {IvStatus::BelowIntrinsic, 0, 0};
    if (price >= upper) return {IvStatus::AboveMax, 0, 0};
    if (price - lower < 1e-12 * std::max(F, K)) return {IvStatus::TooSmall, 0, 0};

    // Bracket: price is increasing in vol.
    double lo = 1e-8, hi = 5.0;
    while (black76_price(F, K, T, hi, call) < price && hi < 1e4) hi *= 2;
    if (black76_price(F, K, T, hi, call) < price) return {IvStatus::NoConvergence, 0, 0};

    // Start near the ATM approximation price ~ 0.4 F sigma sqrt(T), kept inside the bracket.
    double vol = std::clamp(std::sqrt(2.0 * std::numbers::pi / T) * price / F, 2 * lo, hi / 2);
    // Run to machine precision: stop on an exact hit, or once a step no longer moves vol (Newton
    // has converged as far as doubles allow), or the bracket has collapsed. Return the vol with
    // the smallest price error seen. A price tolerance scaled by F would stop too early at BTC
    // prices (1e-14 * 77,000 = 7.7e-10 > the 1e-10 round-trip requirement).
    constexpr double eps = std::numeric_limits<double>::epsilon();
    double best_vol = vol, best_err = std::numeric_limits<double>::infinity();
    for (int i = 1; i <= 200; ++i) {
        const Greeks g = black76(F, K, T, vol, call);
        const double diff = g.price - price;
        if (std::fabs(diff) < best_err) {
            best_err = std::fabs(diff);
            best_vol = vol;
        }
        if (diff == 0) return {IvStatus::Ok, vol, i};
        if (diff > 0) hi = vol; else lo = vol;
        double next = g.vega > 1e-300 ? vol - diff / g.vega : -1.0;
        if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
        if (std::fabs(next - vol) <= 2 * eps * vol || hi - lo <= 4 * eps * hi) {
            return {IvStatus::Ok, best_vol, i};
        }
        vol = next;
    }
    return {IvStatus::NoConvergence, best_vol, 200};
}

double hagan_lognormal_vol(double F, double K, double T, double alpha, double beta, double rho,
                           double nu) {
    using std::log;
    using std::pow;
    using std::sqrt;
    if (!(-1.0 < rho && rho < 1.0)) {
        throw std::invalid_argument("rho must lie strictly inside (-1, 1), got " + std::to_string(rho));
    }
    if (alpha <= 0 || F <= 0 || K <= 0) {
        throw std::invalid_argument("alpha, F and K must all be positive");
    }

    // At-the-money limit (the general formula has a 0/0 there).
    if (std::fabs(F - K) < 1e-12) {
        const double Fb = pow(F, 1 - beta);
        const double corr = (pow(1 - beta, 2) / 24) * pow(alpha, 2) / pow(Fb, 2) +
                            0.25 * rho * beta * nu * alpha / Fb + (2 - 3 * pow(rho, 2)) / 24 * pow(nu, 2);
        return alpha / Fb * (1 + corr * T);
    }

    const double log_fk = log(F / K);
    const double fk_beta = pow(F * K, (1 - beta) / 2);
    const double z = (nu / alpha) * fk_beta * log_fk;

    double z_over_x;
    if (std::fabs(z) < 1e-7) {
        z_over_x = 1.0 + 0.5 * rho * z + (2.0 - 3.0 * pow(rho, 2)) * pow(z, 2) / 12.0;
    } else {
        const double x = log((sqrt(1 - 2 * rho * z + z * z) + z - rho) / (1 - rho));
        z_over_x = z / x;
    }

    const double a = alpha / (fk_beta * (1 + (pow(1 - beta, 2) / 24) * pow(log_fk, 2) +
                                         (pow(1 - beta, 4) / 1920) * pow(log_fk, 4)));
    const double b = 1 + ((pow(1 - beta, 2) / 24) * pow(alpha, 2) / pow(fk_beta, 2) +
                          0.25 * rho * beta * nu * alpha / fk_beta + (2 - 3 * pow(rho, 2)) / 24 * pow(nu, 2)) *
                             T;
    return a * z_over_x * b;
}

}  // namespace mm::pricing
