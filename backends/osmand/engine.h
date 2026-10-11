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
    virtual int pointer(const MockNavBackendPointerV1&) = 0;
    virtual ReleaseResult release(ReleaseMode) = 0;
};

using EngineFactory = std::unique_ptr<Engine> (*)();
std::unique_ptr<Engine> make_engine();

} // namespace navicore_osmand
