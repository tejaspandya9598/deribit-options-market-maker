// Option pricing: Black-76 on the forward, implied vol, and Hagan's SABR vol.
//
// Conventions
//   - Black-76 with r = 0 (Deribit's tickers report interest_rate 0), on the forward F
//     (the ticker's underlying_price for that expiry). Prices and Greeks are in forward units
//     (USD for BTC options) per 1 unit of underlying.
//   - T in years, sigma as a decimal (0.45 = 45 vol points).
//   - vega per 1.00 of vol; theta per year of calendar time (dV/dt = -dV/dT).
//   - Deribit BTC options are inverse: premium in BTC = USD price / F. to_btc() and
//     inverse_delta() convert.
#pragma once

#include <cstdint>

namespace mm::pricing {

struct Greeks {
    double price = 0, delta = 0, gamma = 0, vega = 0, theta = 0;
};

// T <= 0 or sigma <= 0: intrinsic value, delta 0/±1 by moneyness, other Greeks 0.
// F <= 0 or K <= 0: all NaN.
double black76_price(double F, double K, double T, double sigma, bool call);
Greeks black76(double F, double K, double T, double sigma, bool call);

inline double to_btc(double usd_price, double F) { return usd_price / F; }
// Delta of the BTC-settled (inverse) option, as Deribit reports it: Black delta minus the
// premium in BTC.
inline double inverse_delta(double black_delta, double price_btc) { return black_delta - price_btc; }

enum class IvStatus : std::uint8_t {
    Ok,
    BadInput,        // F, K, T or price not positive / not finite
    BelowIntrinsic,  // price <= intrinsic value: no vol reproduces it
    AboveMax,        // price >= F (call) or K (put): no vol reproduces it
    TooSmall,        // time value below double resolution (deep in/out of the money)
    NoConvergence,
};

struct IvResult {
    IvStatus status = IvStatus::BadInput;
    double vol = 0;  // meaningful only when status == Ok
    int iterations = 0;
};

// Newton's method on vega, falling back to bisection whenever a step leaves the bracket or
// vega is too small. Never returns NaN: anything unsolvable has a status saying why.
IvResult implied_vol(double price, double F, double K, double T, bool call);

// Port of sabr.model.hagan_lognormal_vol (sabr-vol-calibration v0.2.0): same branches
// (|F-K| < 1e-12 ATM limit, |z| < 1e-7 series) and the same order of operations, so results
// agree to rounding. Throws std::invalid_argument where the Python raises ValueError.
double hagan_lognormal_vol(double F, double K, double T, double alpha, double beta, double rho,
                           double nu);

}  // namespace mm::pricing
