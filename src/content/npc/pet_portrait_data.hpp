#pragma once
#include <map>
#include <string>

namespace d2x {
class DataTable;
class ClassicStrings;
struct PetPortraitDefinition {
    int iconType = 0;
    bool drawLife = false, hireling = false;
    std::string name, baseArt;
    std::map<int, std::string> classArt;
    const std::string &art(int monsterClass) const;
};
std::map<int, PetPortraitDefinition> loadPetPortraitData(const DataTable &, const ClassicStrings &);
} // namespace d2x
