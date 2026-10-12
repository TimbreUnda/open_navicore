#pragma once

// OsmAnd requires its Qt extensions before any Qt header.
#include <OsmAndCore/QtExtensions.h>
#include <QHash>
#include <QString>

#include <cstdint>
#include <memory>

namespace OsmAnd {
class IMapRenderer;
class ObfsCollection;
class ResolvedMapStyle;
struct MapState;
}

namespace navicore_osmand {

class Wakeup;

// What the symbol worker needs to build its own provider graph. Only the
// OBF collection and the resolved style are shared with the renderer; both
// are safe for concurrent readers. Everything stateful (presentation
// environment, primitiviser, object/primitive/symbol providers) is private
// to the worker, so extraction never races the renderer's providers.
struct SymbolSource {
    std::shared_ptr<OsmAnd::ObfsCollection> obfs;
    std::shared_ptr<const OsmAnd::ResolvedMapStyle> style;
    QHash<QString, QString> style_settings;
    QString locale;
};

// Builds a small symbol atlas away from the renderer thread and retains it
// across moving frames. The renderer thread only projects map anchors, rejects
// overlaps, updates a small VBO and draws one GLES2 batch.
class RetainedSymbolLayer {
public:
    RetainedSymbolLayer(SymbolSource source, std::shared_ptr<Wakeup> wakeup);
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
