#include "tob_dump.hpp"

#include <cinttypes>

namespace mm {

TobDump::TobDump(std::FILE* out, const Instruments& reg) : out_(out), reg_(reg) {
    std::fputs("recv_ns,instrument,change_id,valid,bid_px_e8,bid_amt_e8,ask_px_e8,ask_amt_e8\n", out_);
}

void TobDump::on_exchange(const Event& e, ApplyResult, const MarketState& exch, Nanos) {
    const auto* u = std::get_if<BookUpdate>(&e.payload);
    if (u == nullptr) return;
    const OrderBook* b = exch.book(u->id);
    std::fprintf(out_, "%" PRId64 ",%s,%" PRId64 ",%d,", e.recv_ns, reg_.name(u->id).c_str(),
                 u->change_id, b->valid() ? 1 : 0);
    if (auto l = b->best_bid()) {
        std::fprintf(out_, "%" PRId64 ",%" PRId64 ",", l->price, to_e8(l->amount));
    } else {
        std::fputs(",,", out_);
    }
    if (auto l = b->best_ask()) {
        std::fprintf(out_, "%" PRId64 ",%" PRId64 "\n", l->price, to_e8(l->amount));
    } else {
        std::fputs(",\n", out_);
    }
}

}  // namespace mm
