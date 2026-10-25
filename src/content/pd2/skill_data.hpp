#pragma once
#include <cstddef>
#include <string_view>
namespace d2x {
class Archives;
struct ClassicData;
struct SkillSpec;
SkillSpec preparePd2SkillDefinition(const ClassicData &,size_t row);
void preparePd2SkillData(ClassicData &, Archives &);
bool pd2SkillUsesThrowMastery(const ClassicData &,std::string_view primaryType);
}
