#pragma once
#include "gameplay/skills/rule_spec.hpp"
#include "gameplay/skills/visual.hpp"
#include <string>
#include <vector>

namespace d2x {
// Content definition: presentation resources stay outside the authority rule snapshot.
struct SkillSpec : SkillRuleSpec {
    std::string missileArt, castSoundArt;
    SkillOverlayVisual castOverlay, hitOverlay, stateOverlay;
    std::string activationSoundArt;
    std::vector<SkillImpactVisual> impacts;
    std::vector<ProjectileResource> submissileResources;
    std::string impactSoundArt, releaseSoundArt;
    SkillRuleSpec rules() const {
        auto result=static_cast<const SkillRuleSpec &>(*this);
        result.castCue={castOverlay.id,castOverlay.frames,castOverlay.fps};
        result.hitCue={hitOverlay.id,hitOverlay.frames,hitOverlay.fps};
        result.stateCue={stateOverlay.id,stateOverlay.frames,stateOverlay.fps};
        return result;
    }
};
} // namespace d2x
