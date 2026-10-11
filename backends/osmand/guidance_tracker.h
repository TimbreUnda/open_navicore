#pragma once

#include "ui/navigation_model.h"

namespace navicore_osmand {

// Renderer-thread route progress. Coordinates are projected onto the rendered
// route geometry; raw receiver heading/speed values are deliberately excluded
// until their units are qualified.
class GuidanceTracker {
public:
    ui::GuidanceState update(const ui::RouteAlternative& route,
                             const ui::GeoPoint& position) noexcept;
    ui::GuidanceState unavailable(const ui::RouteAlternative& route) const;
    void reset() noexcept { progress_metres_ = 0.0; }

private:
    double progress_metres_ = 0.0;
};

} // namespace navicore_osmand
