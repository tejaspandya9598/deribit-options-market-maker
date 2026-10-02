// Hourly book checkpoints: cache/books/YYYY-MM-DD/HH.jsonl.
//
// Hours are UTC hours of recv_ns, the same split the recorder uses for data/raw/DAY/HH.jsonl.gz.
// So checkpoint HH is exactly the state after every file before HH, and file HH continues it.
// (Splitting on exchange time would put an update stamped 23:59:59.9 but received 00:00:00.05
// both inside checkpoint 00 and in file 00, and replaying it twice breaks the change_id chain.)
//
// Books only get snapshots when the recorder connects or resyncs after a gap, so a replay that
// starts mid-recording would begin with empty books. A checkpoint holds, in the raw-line format:
//   - the latest recorded instrument lists (verbatim)
//   - one synthetic snapshot per valid book, with its real change_id, so the next recorded
//     change still chains onto it
// Replay from hour H:  cat cache/books/DAY/HH.jsonl <(gunzip -c data/raw/DAY/HH..23) | mm_engine
// Feed complete hours only: the live hour's file ends in an unfinished gzip member.
// Every checkpoint line has recv_ns = the hour boundary.
// Derived data: lives in cache/, never data/.
#pragma once

#include <filesystem>
#include <string>

#include "event_loop.hpp"
#include "instruments.hpp"
#include "parse.hpp"

namespace mm {

class CheckpointWriter {
public:
    CheckpointWriter(std::filesystem::path dir, const Instruments& reg, const Parser& parser)
        : dir_(std::move(dir)), reg_(reg), parser_(parser) {}

    // Call before processing each event with its recv_ns. Writes a checkpoint of the state so far
    // whenever recv_ns is in a later UTC hour than the previous event's.
    void before(Nanos recv_ns, const MarketState& exch);
    int written = 0;

private:
    void write(Nanos boundary, const MarketState& exch);

    std::filesystem::path dir_;
    const Instruments& reg_;
    const Parser& parser_;
    Nanos hour_ = -1;
};

}  // namespace mm
