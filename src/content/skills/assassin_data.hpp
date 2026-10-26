#pragma once
#include <cstddef>
#include <memory>
namespace d2x {
struct ClassicData;
struct AssassinSkillSpec;
class Archives;
std::shared_ptr<AssassinSkillSpec> prepareAssassinProgram(const ClassicData &,size_t row);
void loadAssassinSkills(ClassicData &,Archives &);
}
