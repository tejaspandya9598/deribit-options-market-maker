// Basic units shared by the engine.
#pragma once

#include <cmath>
#include <cstdint>

namespace mm {

// Nanoseconds since the Unix epoch. Exchange time unless a name says otherwise (e.g. recv_ns).
using Nanos = std::int64_t;
inline constexpr Nanos kUs = 1'000;
inline constexpr Nanos kMs = 1'000'000;
inline constexpr Nanos kSec = 1'000'000'000;

// Prices as integers in 1e-8 units (BTC for options, USD for the perpetual), so book keys are
// exact and output is deterministic.
using Px = std::int64_t;
inline constexpr double kE8 = 1e8;

// Same expression as check_book.py's to_e8(), so Python and C++ agree bit for bit.
inline std::int64_t to_e8(double v) { return static_cast<std::int64_t>(std::floor(v * kE8 + 0.5)); }
inline double from_e8(std::int64_t v) { return static_cast<double>(v) / kE8; }

// Dense ids, assigned in order of first appearance in the input.
using InstrumentId = std::int32_t;

}  // namespace mm
