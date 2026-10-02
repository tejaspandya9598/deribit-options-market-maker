// Name <-> id registry. Holds names only; instrument data arrives as InstrumentDef events so it
// is never known before its time.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace mm {

class Instruments {
public:
    InstrumentId id(std::string_view name) {
        auto it = ids_.find(std::string(name));
        if (it != ids_.end()) return it->second;
        auto id = static_cast<InstrumentId>(names_.size());
        names_.emplace_back(name);
        ids_.emplace(names_.back(), id);
        return id;
    }
    const std::string& name(InstrumentId id) const { return names_.at(static_cast<size_t>(id)); }
    size_t size() const { return names_.size(); }

private:
    std::unordered_map<std::string, InstrumentId> ids_;
    std::vector<std::string> names_;
};

}  // namespace mm
