#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/items/quality.hpp"
#include <cstdint>
#include <string>

namespace d2x {
enum class SoundActorKind { Player, Monster };
struct SoundActorView {
    EntityId id;
    SoundActorKind kind = SoundActorKind::Monster;
    int identity = -1;
    uint64_t actionRevision = 0;
    Vec position;
    bool alive = true, moving = false, neutral = false, frozen = false, audible = false;
    float movementCycle = 0;
};
struct PresentationSoundEvent {
    enum class Kind { Attack1, Attack2, Skill1, Skill2, Skill3, Skill4, Hit, Death, Cast, LevelUp };
    Kind kind = Kind::Cast;
    EntityId source;
    uint64_t actionRevision = 0;
    int skill = -1;
    float age = 0, releaseTime = -1;
};
struct ItemDropSoundEvent {
    enum class Kind { Flip, Land };
    std::string code;
    ItemQuality quality = ItemQuality::Normal;
    int specialRow = -1;
    float age = 0;
    bool audible = false;
    Kind kind = Kind::Land;
};
} // namespace d2x
