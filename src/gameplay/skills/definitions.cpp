#include "gameplay/model/definitions.hpp"
#include <stdexcept>

namespace d2x {
const SkillDefinition &skillDefinition(Skill id) {
    static const std::array<SkillDefinition, skillCount> skills{
        {{Skill::Fireball, "FIREBALL", "FIREBALL", "Exploding projectile / 65 fire damage", 9, .32f, .32f, 65,
          3, 0, 2, 0, 0, 22},
         {Skill::FrostNova, "FROST NOVA", "FROST", "48 cold damage / slows nearby enemies", 18, 2, .32f, 48,
          8, 0, .7f, 3, 0},
         {Skill::Whirlwind, "WHIRLWIND", "WHIRL", "Spin through enemies / continuous damage", 22, 1.5f, 0, 95,
          3.8f, 9, 1.15f, 0, 0},
         {Skill::Teleport, "TELEPORT", "TELEPORT", "Blink to clear ground / range 22", 24, .8f, .32f, 0, 0,
          22, .55f, 0, 0},
         {Skill::Leap, "LEAP ATTACK", "LEAP", "Leap over obstacles / 60 landing damage", 15, 1.6f, 0, 60, 4,
          14, .8f, 0, 0},
         {Skill::WarCry, "WAR CRY", "WAR CRY", "30 physical damage / stuns for 2.5 sec", 20, 3, .32f, 30, 7,
          0, 1, 0, 2.5f}}};
    auto index = size_t(id);
    if (index >= skills.size())
        throw std::out_of_range("Unknown skill definition");
    return skills[index];
}
}
