#pragma once

#include "config.h"
#include <open_navicore/backend.h>
#include <memory>

namespace navicore_osmand {

class Wakeup;
enum class ReleaseMode { CurrentContext, ContextLost };
enum class ReleaseResult { Released, Quarantined };
enum class FrameOutcome { Pending, Presented, Fault };
enum class PendingStage { None, Prepare, Render };

struct FrameResult {
    FrameOutcome outcome;
    PendingStage pending = PendingStage::None;
    bool request_again = false;
};

// Only the concrete engine includes Qt/OsmAnd. A failed/throwing initialize
// must still be followed by release, which reports whether destruction is safe.
class Engine {
public:
    virtual ~Engine() = default;
    virtual bool initialize(const Config&, const MockNavBackendRenderTargetV1&,
                            std::shared_ptr<Wakeup>) = 0;
    virtual FrameResult frame(const MockNavBackendRenderTargetV1&) = 0;
    virtual int gps(const MockNavBackendGpsV1&) = 0;
    virtual int pointer(const MockNavBackendPointerV2&) = 0;
    virtual ReleaseResult release(ReleaseMode) = 0;
    // Platform inputs (backend V2-V5). Most carry raw values whose meaning
    // is not qualified yet (api-ui-restrictions.md): recorded and shown,
    // not acted on. The session replays the latest of each after attach.
    virtual void visibility(const MockNavBackendVisibilityV1&) {}
    virtual void screen(const MockNavBackendScreenV1&) {}
    virtual void vehicle(const MockNavBackendVehicleV2&) {}
    virtual void internationalization(const MockNavBackendInternationalizationV1&) {}
    virtual void cluster_display(const MockNavBackendClusterDisplayV1&) {}
    virtual void driver_distraction(const MockNavBackendDriverDistractionV1&) {}
    // Outbound services (cluster, audio, thread status) from start until
    // stop; null when the runtime gave none. Call on the renderer thread.
    virtual void set_outbound(const MockNavBackendServicesV2*) {}
};

using EngineFactory = std::unique_ptr<Engine> (*)();
std::unique_ptr<Engine> make_engine();

} // namespace navicore_osmand
