// Items released at a set time. Ties break by push order (sequence number), so runs are
// deterministic.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "types.hpp"

namespace mm {

template <class T>
class DelayQueue {
public:
    struct Item {
        Nanos release;
        std::uint64_t seq;
        T value;
    };

    // seq comes from the caller so several queues can share one global order.
    void push(Nanos release, std::uint64_t seq, T value) {
        heap_.push_back({release, seq, std::move(value)});
        std::push_heap(heap_.begin(), heap_.end(), later);
    }
    bool empty() const { return heap_.empty(); }
    size_t size() const { return heap_.size(); }
    const Item& top() const { return heap_.front(); }
    Item pop() {
        std::pop_heap(heap_.begin(), heap_.end(), later);
        Item it = std::move(heap_.back());
        heap_.pop_back();
        return it;
    }

private:
    static bool later(const Item& a, const Item& b) {
        return a.release != b.release ? a.release > b.release : a.seq > b.seq;
    }
    std::vector<Item> heap_;
};

}  // namespace mm
