#pragma once
#include "gameplay/model/definitions.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x {
// Typed values imported from Skills.txt and Missiles.txt. No archive data enters gameplay.
struct OriginalSkillSpec {
    struct OverlayVisual {
        int id = -1, frames = 0, trans = 5;
        float fps = 0;
        Vec offset;
        std::string art;
    };
    struct ImpactVisual {
        int missileId = -1;
        std::string art;
        float duration = 0;
    };
    Skill effect = Skill::Fireball;
    int mana = 0, minimumMana = 0, manaPerLevel = 0, manaShift = 8;
    int minimumDamage = 0, maximumDamage = 0, hitShift = 8;
    bool fireDamage = false;
    bool lightningDamage = false;
    std::array<int, 5> minimumPerLevel{}, maximumPerLevel{};
    int synergyPercent = 0;
    std::vector<int> synergySkills;
    int coldFrames = 0;
    std::array<int, 3> coldFramesPerLevel{};
    int coldSynergySkill = -1, coldSynergyPercent = 0;
    int staticPercent = 0, staticRange = 0, staticRangePerLevel = 0, staticMinDamage = 0;
    int missileId = -1;
    int missileNextDelay = 0;
    int missileVelocityPerLevel = 0, missileRangePerLevel = 0, missileAcceleration = 0, missileMaxVelocity = 0;
    float missileVelocity = 0, missileLifetime = 0, impactRadius = 0;
    std::string missileArt, castSoundArt;
    OverlayVisual castOverlay, hitOverlay;
    std::vector<ImpactVisual> impacts;
    std::string impactSoundArt, releaseSoundArt;
};
struct OriginalSkillCast {
    Skill effect = Skill::Fireball;
    float castDuration = 0, castImpact = 0, castRate = 0;
    float manaCost = 0, minimumDamage = 0, maximumDamage = 0;
    float coldDuration = 0, missileVelocity = 0, missileLifetime = 0, impactRadius = 0;
    int missileId = -1;
    float missileNextDelay = 0;
    float missileAcceleration = 0, missileMaxVelocity = 0;
    float staticPercent = 0, staticRadius = 0, staticMinDamage = 0;
    int castOverlayId = -1, hitOverlayId = -1;
    float visualDuration = 0, hitOverlayDuration = 0;
    int impactMissileId = -1;
    float impactDuration = 0;
};
OriginalSkillCast resolveOriginalSkill(const OriginalSkillSpec &spec, int rank,
                                       const std::map<int, int> &learned, int fireMasteryPercent = 0,
                                       int lightningMasteryPercent = 0);
} // namespace d2x
