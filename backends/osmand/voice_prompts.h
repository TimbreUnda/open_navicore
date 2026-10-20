#pragma once

// Spoken guidance: turns the renderer-thread GuidanceState into the sentences
// to say. Pure logic (no audio, no threads, no clock) so it is host-testable;
// speaking them is the voice worker's job (see docs/design/voice-guidance-wiring.md).
//
// Speech differs from the UI strings: distances are written out with the right
// plural unit word ("200 метрів", "2 кілометри") because the TTS engines read
// digits but not the abbreviations "м"/"км". Languages: Ukrainian and English.
// Other UI languages produce no prompts (no matching voice is installed).

#include "ui/localization.h"
#include "ui/navigation_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace navicore_osmand {

enum class PromptPriority { Maneuver = 1, Reroute = 2, Arrival = 3 };

struct SpokenPrompt {
    std::string text;       // UTF-8, ready for the TTS engine
    PromptPriority priority = PromptPriority::Maneuver;
    std::string key;        // stable id of the announcement, for logs and tests
};

class PromptPlanner {
public:
    // Distances (metres) at which an upcoming manoeuvre is announced, far to near.
    // The last stage is spoken without a distance ("Turn right").
    static constexpr double kStages[] = {1000.0, 500.0, 200.0, 50.0};
    static constexpr std::size_t kStageCount = 4;

    // Prompts that became due with this state, in speaking order (usually 0 or 1).
    // A state without a position, or a simulated one with `speak_simulated`
    // false, produces nothing and does not advance the announcement state.
    std::vector<SpokenPrompt> update(const ui::GuidanceState& state,
                                     ui::loc::Language language,
                                     bool speak_simulated = true);

    // Forget everything (new route, restart).
    void reset() noexcept;

    // The sentence for one stage; exposed for tests and for a "repeat" command.
    static std::string maneuver_sentence(const ui::GuidanceState& state, std::size_t stage,
                                         ui::loc::Language language);
    // "200 метрів", "1 кілометр", "2 кілометри", "1,5 кілометра"; English likewise.
    static std::string spoken_distance(double metres, ui::loc::Language language);

private:
    bool has_maneuver_ = false;
    ui::ManeuverKind kind_ = ui::ManeuverKind::Straight;
    std::size_t geometry_index_ = 0;
    bool stage_done_[kStageCount] = {false, false, false, false};
    bool was_recalculating_ = false;
    bool was_arrived_ = false;
};

} // namespace navicore_osmand
