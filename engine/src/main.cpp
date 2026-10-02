// mm_engine: replays recorded (or live) Deribit data from stdin.
//
//   gunzip -c data/raw/DAY/*.jsonl.gz | mm_engine [options]
//
// Options:
//   --md-latency-ms X      exchange -> strategy latency
//   --order-latency-ms X   strategy -> exchange latency
//                          Default for both: lowest probe round trip in $DATA_DIR/logs / 2
//                          (best case measured). Exits if no probes and no flag.
//   --fits PATH            SABR fit cache (plain JSONL; use <(gunzip -c ...))
//   --tob-out PATH         exchange-side top of book after every book message (CSV)
//   --checkpoint-dir DIR   write hourly book checkpoints (e.g. cache/books)
// Stats go to stderr.
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "checkpoint.hpp"
#include "event_loop.hpp"
#include "input.hpp"
#include "parse.hpp"
#include "tob_dump.hpp"

namespace {

using namespace mm;

#ifdef NDEBUG
constexpr const char* kBuild = "optimized";
#else
constexpr const char* kBuild = "DEBUG (asserts on, not for timing)";
#endif

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "mm_engine: %s\n", msg.c_str());
    std::exit(2);
}

struct ProbeMin {
    double min_rtt_ms = std::numeric_limits<double>::infinity();
    std::int64_t probes = 0;
};

// Lowest recorded probe round trip across all recorder logs.
ProbeMin read_probes(const std::filesystem::path& log_dir) {
    ProbeMin p;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(log_dir, ec)) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with("recorder-") || entry.path().extension() != ".jsonl") continue;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\"event\": \"probe\"") == std::string::npos) continue;
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded() || !j.contains("rtt_ms")) continue;
            p.min_rtt_ms = std::min(p.min_rtt_ms, j["rtt_ms"].get<double>());
            ++p.probes;
        }
    }
    return p;
}

// Latency in ms: a finite number in [0, 60000]. A negative value would deliver events or orders
// before their input time (look-ahead); a huge one would overflow time + latency.
Nanos ms_arg(const char* flag, const char* v) {
    char* end = nullptr;
    double ms = std::strtod(v, &end);
    if (end == v || *end != '\0' || !std::isfinite(ms) || ms < 0 || ms * kMs > kMaxLatency) {
        die(std::string(flag) + " must be a number from 0 to 60000, got '" + v + "'");
    }
    return static_cast<Nanos>(std::llround(ms * kMs));
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<Nanos> md_lat, ord_lat;
    const char* fits_path = nullptr;
    const char* tob_path = nullptr;
    const char* ckpt_dir = nullptr;
    for (int i = 1; i < argc; ++i) {
        auto need = [&](const char* flag) {
            if (i + 1 >= argc) die(std::string(flag) + " needs a value");
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--md-latency-ms")) md_lat = ms_arg(argv[i], need(argv[i]));
        else if (!std::strcmp(argv[i], "--order-latency-ms")) ord_lat = ms_arg(argv[i], need(argv[i]));
        else if (!std::strcmp(argv[i], "--fits")) fits_path = need(argv[i]);
        else if (!std::strcmp(argv[i], "--tob-out")) tob_path = need(argv[i]);
        else if (!std::strcmp(argv[i], "--checkpoint-dir")) ckpt_dir = need(argv[i]);
        else die(std::string("unknown option ") + argv[i]);
    }

    std::string latency_source = "flags";
    if (!md_lat || !ord_lat) {
        const char* data_dir = std::getenv("DATA_DIR");
        auto log_dir = std::filesystem::path(data_dir ? data_dir : "data") / "logs";
        ProbeMin p = read_probes(log_dir);
        if (p.probes == 0) die("no latency probes in " + log_dir.string() + "; pass --md-latency-ms and --order-latency-ms");
        Nanos one_way = static_cast<Nanos>(std::llround(p.min_rtt_ms * kMs / 2));
        if (!md_lat) md_lat = one_way;
        if (!ord_lat) ord_lat = one_way;
        char buf[160];
        std::snprintf(buf, sizeof buf, "lowest probe RTT %.3f ms / 2 over %" PRId64 " probes in %s",
                      p.min_rtt_ms, p.probes, log_dir.c_str());
        latency_source = buf;
    }
    std::fprintf(stderr, "latency: md %.3f ms, order %.3f ms (%s)\n", *md_lat / 1e6, *ord_lat / 1e6,
                 latency_source.c_str());

    Instruments reg;
    Parser parser(reg);
    LineReader main_in(stdin);
    std::unique_ptr<LineReader> fits_in;
    std::FILE* fits_file = nullptr;
    if (fits_path) {
        fits_file = std::fopen(fits_path, "r");
        if (!fits_file) die(std::string("cannot open ") + fits_path);
        fits_in = std::make_unique<LineReader>(fits_file);
    }
    MergedInput input(main_in, fits_in.get(), parser);

    EventLoop loop({*md_lat, *ord_lat});
    std::FILE* tob_file = nullptr;
    std::unique_ptr<TobDump> tob;
    if (tob_path) {
        tob_file = std::fopen(tob_path, "w");
        if (!tob_file) die(std::string("cannot write ") + tob_path);
        std::setvbuf(tob_file, nullptr, _IOFBF, 1 << 20);
        tob = std::make_unique<TobDump>(tob_file, reg);
        loop.add_exchange_observer(tob.get());
    }
    std::unique_ptr<CheckpointWriter> ckpt;
    if (ckpt_dir) ckpt = std::make_unique<CheckpointWriter>(ckpt_dir, reg, parser);

    auto t0 = std::chrono::steady_clock::now();
    Event e;
    while (input.next(e)) {
        // Raw files rotate on recv_ns, so checkpoints must too (fits have no recv_ns).
        if (ckpt && e.recv_ns) ckpt->before(e.recv_ns, loop.exchange());
        loop.process(std::move(e));
    }
    loop.finish();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (tob_file) std::fclose(tob_file);
    if (fits_file) std::fclose(fits_file);

    const auto& s = loop.stats();
    std::fprintf(stderr,
                 "lines %" PRId64 ", malformed %" PRId64 ", ignored %" PRId64 ", events %" PRId64 "\n"
                 "books %" PRId64 " (snapshots %" PRId64 ", gaps %" PRId64 ", no_snapshot %" PRId64 ")"
                 ", trades %" PRId64 ", tickers %" PRId64 ", index %" PRId64 ", fits %" PRId64
                 ", defs %" PRId64 ", recorder %" PRId64 "\n"
                 "late events %" PRId64 ", fits out of order %" PRId64 ", checkpoints %d\n"
                 "wall %.2f s (%.0f events/s), %s build\n",
                 main_in.lines, parser.malformed, parser.ignored, s.events, s.books, s.snapshots,
                 s.gaps, s.no_snapshot, s.trades, s.tickers, s.index, s.fits, s.defs, s.recorder,
                 s.late_events, input.fits_out_of_order, ckpt ? ckpt->written : 0, secs,
                 secs > 0 ? s.events / secs : 0.0, kBuild);
    return 0;
}
