#include "event_loop.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace mm {

EventLoop::EventLoop(LoopConfig cfg) : cfg_(cfg) {
    auto bad = [](Nanos l) { return l < 0 || l > kMaxLatency; };
    if (bad(cfg_.md_latency) || bad(cfg_.order_latency)) {
        throw std::invalid_argument("EventLoop: latencies must be between 0 and 60 s");
    }
}

void EventLoop::release_until(Nanos t) {
    while (true) {
        bool md = !md_q_.empty() && md_q_.top().release <= t;
        bool ord = !order_q_.empty() && order_q_.top().release <= t;
        if (!md && !ord) return;
        // Both due: the earlier (release, seq) goes first.
        if (md && ord) {
            const auto& a = md_q_.top();
            const auto& b = order_q_.top();
            md = a.release != b.release ? a.release < b.release : a.seq < b.seq;
        }
        if (md) {
            auto it = md_q_.pop();
            now_ = std::max(now_, it.release);
            view_.apply(it.value);
            for (auto* o : market_obs_) o->on_market(it.value, view_, now_);
        } else {
            auto it = order_q_.pop();
            now_ = std::max(now_, it.release);
            for (auto* o : exch_obs_) o->on_order(it.value, exch_, now_);
        }
    }
}

void EventLoop::process(Event e) {
    const bool is_fit = std::holds_alternative<SabrFit>(e.payload);
    if (e.time == 0) e.time = now_;  // recorder events carry no exchange time
    release_until(e.time);
    if (e.time < now_) ++stats_.late_events;
    now_ = std::max(now_, e.time);

    ApplyResult r = ApplyResult::NotBook;
    if (!is_fit) {
        r = exch_.apply(e);
        for (auto* o : exch_obs_) o->on_exchange(e, r, exch_, now_);
    }
    count(e, r);

    // Nobody reads the view without market observers; skip the copy.
    if (!market_obs_.empty()) {
        Nanos release = is_fit ? e.time : e.time + cfg_.md_latency;
        md_q_.push(release, seq_++, std::move(e));
    }
}

void EventLoop::finish() { release_until(std::numeric_limits<Nanos>::max()); }

void EventLoop::submit_order(const OrderRequest& o) {
    order_q_.push(now_ + cfg_.order_latency, seq_++, o);
}

void EventLoop::count(const Event& e, ApplyResult r) {
    ++stats_.events;
    switch (e.payload.index()) {
        case 0:
            ++stats_.books;
            if (r == ApplyResult::Snapshot) ++stats_.snapshots;
            if (r == ApplyResult::Gap) ++stats_.gaps;
            if (r == ApplyResult::NoSnapshot) ++stats_.no_snapshot;
            break;
        case 1: ++stats_.trades; break;
        case 2: ++stats_.tickers; break;
        case 3: ++stats_.index; break;
        case 4: ++stats_.fits; break;
        case 5: ++stats_.defs; break;
        case 6: ++stats_.recorder; break;
        default: break;
    }
}

}  // namespace mm
