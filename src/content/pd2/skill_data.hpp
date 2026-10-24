#pragma once
#include <cstddef>
namespace d2x {
class Archives;
struct ClassicData;
struct SkillSpec;
SkillSpec preparePd2SkillDefinition(const ClassicData &,size_t row);
void preparePd2SkillData(ClassicData &, Archives &);
}
