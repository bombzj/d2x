#pragma once
#include "gameplay/model/definitions.hpp"

namespace d2x {
class Simulation;
class SkillSystem {
    using Effect = void (*)(Simulation &, Vec, const SkillDefinition &);
    using Validator = bool (*)(const Simulation &, Vec, const SkillDefinition &);
    static bool clearGround(const Simulation &simulation, Vec target, const SkillDefinition &skill);
    static void fireball(Simulation &simulation, Vec target, const SkillDefinition &skill);
    static void frostNova(Simulation &simulation, Vec target, const SkillDefinition &skill);
    static void whirlwind(Simulation &simulation, Vec target, const SkillDefinition &skill);
    static void teleport(Simulation &simulation, Vec target, const SkillDefinition &skill);
    static void leap(Simulation &simulation, Vec target, const SkillDefinition &skill);
    static void warCry(Simulation &simulation, Vec target, const SkillDefinition &skill);

  public:
    static bool cast(Simulation &simulation, Skill id, Vec target);
};
} // namespace d2x