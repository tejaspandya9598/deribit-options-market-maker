// Raw recorder lines -> events.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "events.hpp"
#include "instruments.hpp"

namespace mm {

class Parser {
public:
    explicit Parser(Instruments& reg) : reg_(reg) {}

    // One data/raw line: {"recv_ns":..,"msg":{...}} or {"recv_ns":..,"event":{...}}, or a
    // fit-cache line (see parse_fit_line), which a live feed mixes into the same stream.
    // Appends zero or more events (a trades message can hold several trades). Malformed lines
    // are counted and skipped; they never stop a run.
    void parse_line(std::string_view line, std::vector<Event>& out);

    // One fit-cache line (see SabrFit). Returns false if malformed.
    bool parse_fit_line(std::string_view line, Event& out);

    std::int64_t malformed = 0;
    std::int64_t ignored = 0;  // well-formed but not used (e.g. subscribe replies)

    // Latest instrument-list responses (the raw "msg" object, verbatim), for book checkpoints.
    std::string last_options_msg, last_perp_msg;

private:
    Instruments& reg_;
};

}  // namespace mm
