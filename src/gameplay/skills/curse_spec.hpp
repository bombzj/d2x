#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/effects/unit_event.hpp"
#include "gameplay/character/attributes.hpp"
#include "core/math.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <optional>
#include <vector>

namespace d2x {
struct CurseSpec {
    enum class TargetProcedure { Necromancer, DirectList, TerrorUtility } targetProcedure{TargetProcedure::Necromancer};
    CombatStateDefinition state;
    int targetFilter = 0; // Skills.aurafilter: original player/monster type bits.
    int radius = 0, radiusPerLevel = 0, frames = 0, framesPerLevel = 0;
    CharacterModifiers modifiers;
    CurseAi ai = CurseAi::None;
    bool singleTarget = false, skipActive = false, ignoreDifficultyDivisor = false;
    int targetLevelBonus = 0;
    bool permanent = false;
    std::optional<Vec> fearOrigin;
    EntityId fearSource;
    int attackRatingPerLevel = 0, damagePerLevel = 0, defensePerLevel = 0;
    int resistMinimum = 0, resistMaximum = 0;
    int reflectPercent = 0, reflectPerLevel = 0;
    int reflectReducedDivisor = 1, reflectHitClass = -1;
    int lifeTapPercent = 0, lifeTapPerLevel = 0;
    int healOverlay = -1;
    float healOverlayDuration = 0;
    std::optional<std::map<StatKey,int32_t>> nativeValues;
    std::vector<StatValue> nativeWrites;
    std::optional<StatKey> primaryStat;
    bool rejectZeroPrimary{true};
    std::vector<SkillUnitEventBinding> unitEvents;
};
} // namespace d2x
