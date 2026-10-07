// Input: raw recorder lines (stdin) merged by time with an optional SABR fit file.
//
// Plain files only, no zlib: pass gzipped files through gunzip, e.g.
//   gunzip -c data/raw/DAY/*.jsonl.gz | mm_engine --fits <(gunzip -c cache/sabr/DAY.jsonl.gz)
#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

#include "events.hpp"
#include "parse.hpp"

namespace mm {

class LineReader {
public:
    explicit LineReader(std::FILE* f) : f_(f) {}
    ~LineReader();
    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;
    // Next line without the trailing newline; false at end of file.
    bool next(std::string_view& line);
    std::int64_t lines = 0;

private:
    std::FILE* f_;
    char* buf_ = nullptr;
    size_t cap_ = 0;
};

class MergedInput {
public:
    MergedInput(LineReader& main, LineReader* fits, Parser& parser)
        : main_(main), fits_(fits), parser_(parser) {}

    // Next event: a fit comes first when its time is at or before the next market event's.
    bool next(Event& out);

    std::int64_t fits_out_of_order = 0;
    // True once the market input has ended; any events still returned are leftover fits.
    bool main_exhausted() const { return main_done_; }

private:
    bool fill_main();
    bool fill_fit();

    LineReader& main_;
    LineReader* fits_;
    Parser& parser_;
    std::deque<Event> pending_;
    std::vector<Event> scratch_;
    std::optional<Event> next_fit_;
    Nanos last_main_time_ = 0;
    Nanos last_fit_time_ = 0;
    bool main_done_ = false;
};

}  // namespace mm
