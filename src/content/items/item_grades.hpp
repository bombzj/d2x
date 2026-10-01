#pragma once
#include "content/classic_data.hpp"

namespace d2x {
void loadItemGrades(ClassicData &data);
bool isGradeEligible(const QualityGradeRecord &grade, const ItemDefinition &item);
struct GradeGenerationResult {
    ItemGeneration generation;
    uint64_t randomState = 0;
    std::string deferred;
};
GradeGenerationResult rollItemGrade(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, uint64_t seed);
} // namespace d2x
