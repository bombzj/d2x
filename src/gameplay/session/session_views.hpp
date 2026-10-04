#pragma once
#include "core/math.hpp"
#include "content/npc/hireling_data.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/quest/state.hpp"
#include "gameplay/quest/prelude.hpp"
#include <array>
#include <optional>
#include <string>

namespace d2x {
struct NpcSpeech;
struct ShrineStatus {
    int code = 0;
    std::string name, effect;
    float until = 0;
    EffectHandle stateEffect;
};
struct NpcQuestDialogue {
    const NpcSpeech *speech = nullptr;
    std::optional<QuestId> advancesQuest;
    bool automatic = false;
    std::string readKey;
    std::optional<QuestPreludeId> prelude;
    bool alert = false;
    uint32_t staffExplanation = 0;
};
struct HirelingCombatStats {
    HirelingStats base;
    int fireResist = 0, coldResist = 0, lightningResist = 0, poisonResist = 0;
    WeaponDamage weapon;
    CombatModifiers combat;
    int displayDamageMin = 0, displayDamageMax = 0;
    int fasterMoveVelocity = 0, velocityPercent = 0, vitality = 0;
};
struct SessionPortalView { uint64_t revision; Vec position; float openedAt; };
} // namespace d2x
