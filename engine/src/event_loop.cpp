#include "event_loop.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace mm {

namespace {

size_t late_bucket(Nanos late) {
    size_t i = 0;
    while (i < kLateEdgesMs.size() && late > kLateEdgesMs[i] * kMs) ++i;
    return i;
}

}  // namespace

EventLoop::EventLoop(LoopConfig cfg) : cfg_(cfg) {
    auto bad = [](Nanos l) { return l < 0 || l > kMaxLatency; };
    if (bad(cfg_.md_latency) || bad(cfg_.order_latency) || bad(cfg_.recorded_latency)) {
        throw std::invalid_argument("EventLoop: latencies must be between 0 and 60 s");
    }
}

void EventLoop::release_until(Nanos t) {
    while (true) {
        // The due item with the earliest (release, seq) across the three queues goes first.
        int pick = -1;
        Nanos best_release = 0;
        std::uint64_t best_seq = 0;
        auto consider = [&](int which, bool empty, Nanos release, std::uint64_t seq) {
            if (empty || release > t) return;
            if (pick < 0 || release < best_release || (release == best_release && seq < best_seq)) {
                pick = which;
                best_release = release;
                best_seq = seq;
            }
        };
        consider(0, md_q_.empty(), md_q_.empty() ? 0 : md_q_.top().release,
                 md_q_.empty() ? 0 : md_q_.top().seq);
        consider(1, order_q_.empty(), order_q_.empty() ? 0 : order_q_.top().release,
                 order_q_.empty() ? 0 : order_q_.top().seq);
        consider(2, update_q_.empty(), update_q_.empty() ? 0 : update_q_.top().release,
                 update_q_.empty() ? 0 : update_q_.top().seq);
        if (pick < 0) return;

        now_ = std::max(now_, best_release);
        if (pick == 0) {
            auto it = md_q_.pop();
            view_.apply(it.value);
            for (auto* o : market_obs_) o->on_market(it.value, view_, now_);
        } else if (pick == 1) {
            auto it = order_q_.pop();
            for (auto* o : exch_obs_) o->on_order(it.value, exch_, now_);
        } else {
            auto it = update_q_.pop();
            for (auto* o : order_obs_) o->on_order_update(it.value, view_, now_);
        }
    }
}

void EventLoop::process(Event e) {
    const bool is_fit = std::holds_alternative<SabrFit>(e.payload);
    if (e.time == 0) e.time = now_;  // recorder events carry no exchange time
    release_until(e.time);
    if (e.time < now_) {
        const Nanos late = now_ - e.time;
        ++stats_.late_events;
        ++stats_.late_hist[late_bucket(late)];
        stats_.max_late = std::max(stats_.max_late, late);
        if (std::holds_alternative<Trade>(e.payload)) {
            ++stats_.late_trades;
            ++stats_.late_trade_hist[late_bucket(late)];
            stats_.max_late_trade = std::max(stats_.max_late_trade, late);
        }
    }
    now_ = std::max(now_, e.time);

    ApplyResult r = ApplyResult::NotBook;
    if (!is_fit) {
        for (auto* o : exch_obs_) o->on_clock(now_, exch_);
        r = exch_.apply(e);
        for (auto* o : exch_obs_) o->on_exchange(e, r, exch_, now_);
    }
    count(e, r);

    // Nobody reads the view without market observers; skip the copy.
    if (!market_obs_.empty()) {
        Nanos release;
        if (cfg_.md_timing == MdTiming::AsReceived) {
            if (is_fit) {
                // A slower link delays the fit's inputs, so the fit with them. A faster link is
                // not credited: a fit is never delivered before its own fit time + compute.
                release = e.time + std::max<Nanos>(0, cfg_.md_latency - cfg_.recorded_latency);
            } else {
                // When the event left the exchange's feed, as far as the recording can tell.
                Nanos sent = e.time;
                if (e.recv_ns != 0) sent = std::max(e.time, e.recv_ns - cfg_.recorded_latency);
                release = sent + cfg_.md_latency;
            }
        } else {
            release = is_fit ? e.time : e.time + cfg_.md_latency;
        }
        md_q_.push(release, seq_++, std::move(e));
    }
}

void EventLoop::finish() { release_until(std::numeric_limits<Nanos>::max()); }

void EventLoop::submit_order(const OrderRequest& o) {
    order_q_.push(now_ + cfg_.order_latency, seq_++, o);
}

void EventLoop::notify(const OrderUpdate& u) {
    update_q_.push(u.exch_time + cfg_.md_latency, seq_++, u);
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
