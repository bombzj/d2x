#include "gameplay/model/definitions.hpp"
#include <stdexcept>

namespace d2x {
const SkillDefinition &skillDefinition(Skill id) {
    static const std::array<SkillDefinition, skillCount> skills{
        {{Skill::Fireball, "FIREBALL", "FIREBALL", "Original MPQ fire projectile", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::FrostNova, "FROST NOVA", "FROST", "Original MPQ cold nova", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::Whirlwind, "WHIRLWIND", "WHIRL", "Spin through enemies / continuous damage", 22, 1.5f, 0, 95,
          3.8f, 9, 1.15f, 0, 0},
         {Skill::Teleport, "TELEPORT", "TELEPORT", "Original MPQ teleport", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::Leap, "LEAP ATTACK", "LEAP", "Leap over obstacles / 60 landing damage", 15, 1.6f, 0, 60, 4,
          14, .8f, 0, 0},
         {Skill::WarCry, "WAR CRY", "WAR CRY", "30 physical damage / stuns for 2.5 sec", 20, 3, .32f, 30, 7,
          0, 1, 0, 2.5f},
         {Skill::FireBolt, "FIRE BOLT", "FIRE BOLT", "Original MPQ fire projectile", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::StaticField, "STATIC FIELD", "STATIC FIELD", "Original MPQ life reduction", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::IceBolt, "ICE BOLT", "ICE BOLT", "Original MPQ cold projectile", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::Nova, "NOVA", "NOVA", "Original MPQ lightning missiles", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::IceBlast, "ICE BLAST", "ICE BLAST", "Original MPQ freezing projectile", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::ChargedBolt, "CHARGED BOLT", "CHARGED BOLT", "Original MPQ charged bolts", 0, 0, .32f, 0,
          0, 0, 0, 0, 0},
         {Skill::FrozenArmor, "FROZEN ARMOR", "FROZEN ARMOR", "Original MPQ defensive armor", 0, 0, .32f, 0,
          0, 0, 0, 0, 0}}};
    auto index = size_t(id);
    if (index >= skills.size())
        throw std::out_of_range("Unknown skill definition");
    return skills[index];
}
}
