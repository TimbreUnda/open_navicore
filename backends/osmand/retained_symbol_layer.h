#pragma once

#include <cstdint>
#include <memory>

namespace OsmAnd {
class IMapRenderer;
class MapObjectsSymbolsProvider;
struct MapState;
}

namespace navicore_osmand {

class Wakeup;

// Builds a small symbol atlas away from the renderer thread and retains it
// across moving frames. The renderer thread only projects map anchors, rejects
// overlaps, updates a small VBO and draws one GLES2 batch.
class RetainedSymbolLayer {
public:
    RetainedSymbolLayer(
        std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> provider,
        std::shared_ptr<Wakeup> wakeup);
    ~RetainedSymbolLayer();
    RetainedSymbolLayer(const RetainedSymbolLayer&) = delete;
    RetainedSymbolLayer& operator=(const RetainedSymbolLayer&) = delete;

    void request(const OsmAnd::MapState& state);
    bool present(OsmAnd::IMapRenderer& renderer,
                 std::uint32_t width, std::uint32_t height);
    void release(bool context_lost) noexcept;
    void stop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace navicore_osmand
