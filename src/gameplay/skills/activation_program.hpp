#pragma once

namespace d2x {
struct SkillCastSpec;
enum class SkillActivationProgram {
    Weapon, Item, Unsummon, Kick, AmazonSummon, AmazonMagic,
    Teleport, Hydra, Leap, GrimWard, Effect, Warcry, Curse, Corpse, CorpseSearch,
    StaticField, Telekinesis, Missile, Assassin, Unsupported, Count
};
SkillActivationProgram skillActivationProgram(const SkillCastSpec &skill);
}
