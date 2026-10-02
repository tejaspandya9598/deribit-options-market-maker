#include "checkpoint.hpp"

#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace mm {

namespace {

constexpr Nanos kHour = 3600 * kSec;

// Exact decimal of a 1e-8 integer, e.g. 415000 -> "0.00415", 8430250000000 -> "84302.5".
void put_e8(std::string& s, Px v) {
    if (v < 0) {
        s += '-';
        v = -v;
    }
    s += std::to_string(v / 100'000'000);
    auto frac = std::to_string(v % 100'000'000);
    frac.insert(0, 8 - frac.size(), '0');
    while (frac.size() > 1 && frac.back() == '0') frac.pop_back();
    s += '.';
    s += frac;
}

void put_double(std::string& s, double d) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, d);  // shortest round-trip form
    s.append(buf, r.ptr);
}

template <class Side>
void put_side(std::string& s, const Side& side) {
    s += '[';
    bool first = true;
    for (const auto& [px, amt] : side) {
        if (!first) s += ',';
        first = false;
        s += "[\"new\",";
        put_e8(s, px);
        s += ',';
        put_double(s, amt);
        s += ']';
    }
    s += ']';
}

}  // namespace

void CheckpointWriter::before(Nanos recv_ns, const MarketState& exch) {
    Nanos h = recv_ns / kHour;
    if (hour_ >= 0 && h > hour_) write(h * kHour, exch);
    if (h > hour_) hour_ = h;
}

void CheckpointWriter::write(Nanos boundary, const MarketState& exch) {
    std::time_t secs = boundary / kSec;
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char day[16], hour[8];
    std::strftime(day, sizeof day, "%Y-%m-%d", &tm);
    std::strftime(hour, sizeof hour, "%H", &tm);
    auto dir = dir_ / day;
    std::filesystem::create_directories(dir);
    auto path = dir / (std::string(hour) + ".jsonl");
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) throw std::runtime_error("cannot write checkpoint " + path.string());

    for (const auto* msg : {&parser_.last_options_msg, &parser_.last_perp_msg}) {
        if (msg->empty()) continue;
        std::fprintf(f, "{\"recv_ns\":%" PRId64 ",\"checkpoint\":true,\"msg\":%s}\n", boundary,
                     msg->c_str());
    }
    std::string s;
    const auto& books = exch.books();
    for (size_t i = 0; i < books.size(); ++i) {
        const auto& b = books[i];
        if (!b.valid()) continue;
        const auto& name = reg_.name(static_cast<InstrumentId>(i));
        s.clear();
        s += "{\"recv_ns\":" + std::to_string(boundary) + ",\"checkpoint\":true,\"msg\":{";
        s += "\"jsonrpc\":\"2.0\",\"method\":\"subscription\",\"params\":{\"channel\":\"book.";
        s += name + ".100ms\",\"data\":{\"timestamp\":" + std::to_string(b.last_update() / kMs);
        s += ",\"type\":\"snapshot\",\"change_id\":" + std::to_string(b.change_id());
        s += ",\"instrument_name\":\"" + name + "\",\"bids\":";
        put_side(s, b.bids());
        s += ",\"asks\":";
        put_side(s, b.asks());
        s += "}}}}\n";
        std::fputs(s.c_str(), f);
    }
    std::fclose(f);
    ++written;
}

}  // namespace mm
