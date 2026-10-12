#pragma once

#include "tile_cache.h"

// OsmAnd requires its Qt extensions before any Qt header.
#include <OsmAndCore/QtExtensions.h>
#include <QHash>
#include <QString>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace OsmAnd {
class ObfsCollection;
class UnresolvedMapStyle;
class ResolvedMapStyle;
}

namespace navicore_osmand {

// Renders every tile of a pre-cache manifest into a pack store, for shipping
// a region with the radio or filling the cache when parked. It owns a private
// OBF/primitives/raster provider graph (nothing is shared with the renderer),
// runs below render priority, skips tiles already present, and backs off
// while the vehicle moves. Progress is traced and written to
// "<manifest>.progress" as "<done> <total> <failed>".
class PrecacheWorker {
public:
    struct Job {
        std::string manifest_path;
        std::vector<TileRange> ranges;
        std::shared_ptr<OsmAnd::ObfsCollection> obfs;
        std::shared_ptr<const OsmAnd::ResolvedMapStyle> style;
        QHash<QString, QString> style_settings;
        QString locale;
        TilePackStore::Options store;
    };

    explicit PrecacheWorker(Job job);
    ~PrecacheWorker();
    PrecacheWorker(const PrecacheWorker&) = delete;
    PrecacheWorker& operator=(const PrecacheWorker&) = delete;

    void set_throttled(bool throttled) noexcept { throttled_ = throttled; }
    bool finished() const noexcept { return finished_; }
    void stop() noexcept;

private:
    void run() noexcept;
    void write_progress(std::uint64_t done, std::uint64_t total,
                        std::uint64_t failed) const;

    Job job_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> throttled_{false};
    std::atomic<bool> finished_{false};
    std::thread thread_;
};

} // namespace navicore_osmand
