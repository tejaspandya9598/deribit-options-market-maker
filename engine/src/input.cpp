#include "input.hpp"

#include <cstdlib>
#include <utility>

namespace mm {

LineReader::~LineReader() { std::free(buf_); }

bool LineReader::next(std::string_view& line) {
    ssize_t n = ::getline(&buf_, &cap_, f_);
    if (n < 0) return false;
    if (n > 0 && buf_[n - 1] == '\n') --n;
    line = std::string_view(buf_, static_cast<size_t>(n));
    ++lines;
    return true;
}

bool MergedInput::fill_main() {
    std::string_view line;
    while (pending_.empty() && main_.next(line)) {
        if (line.empty()) continue;
        scratch_.clear();
        parser_.parse_line(line, scratch_);
        for (auto& e : scratch_) pending_.push_back(std::move(e));
    }
    if (pending_.empty()) main_done_ = true;
    return !pending_.empty();
}

bool MergedInput::fill_fit() {
    if (next_fit_ || fits_ == nullptr) return next_fit_.has_value();
    std::string_view line;
    Event e;
    while (fits_->next(line)) {
        if (line.empty() || !parser_.parse_fit_line(line, e)) continue;
        if (e.time < last_fit_time_) ++fits_out_of_order;
        last_fit_time_ = e.time;
        next_fit_ = std::move(e);
        return true;
    }
    return false;
}

bool MergedInput::next(Event& out) {
    bool have_main = fill_main();
    bool have_fit = fill_fit();
    if (!have_main && !have_fit) return false;
    if (have_fit) {
        // Recorder events have no exchange time; they sit at the last market time.
        Nanos main_t = have_main ? (pending_.front().time ? pending_.front().time : last_main_time_) : 0;
        if (!have_main || next_fit_->time <= main_t) {
            out = std::move(*next_fit_);
            next_fit_.reset();
            return true;
        }
    }
    out = std::move(pending_.front());
    pending_.pop_front();
    if (out.time) last_main_time_ = out.time;
    return true;
}

}  // namespace mm
