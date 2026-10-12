#pragma once

#include "navigation_model.h"

#include <string>
#include <vector>

namespace navicore_osmand::ui {

// Persists recent destinations as one tab-separated line per entry:
// id, title, subtitle, latitude, longitude. Saves go to "<path>.tmp" and are
// renamed over the target so a power loss leaves the old or the new file,
// never a torn one. Unreadable or malformed lines are skipped.
class RecentsStore {
public:
    explicit RecentsStore(std::string path) : path_(std::move(path)) {}

    std::vector<Destination> load() const;
    bool save(const std::vector<Destination>& recents) const;

private:
    std::string path_;
};

} // namespace navicore_osmand::ui
