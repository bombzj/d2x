#pragma once
#include "gameplay/skills/aura_spec.hpp"
namespace d2x {
struct ClassicData;
AuraSkillSpec preparePd2Aura(const ClassicData &,int skill);
void preparePd2Auras(ClassicData &);
}
