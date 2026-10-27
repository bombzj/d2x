#pragma once
#include <filesystem>
namespace d2x {
struct ClassicData;
void generateSaveRecipes(const ClassicData &, const std::filesystem::path &recipe,
                         const std::filesystem::path &destination);
void exportSaveManifest(const ClassicData &, const std::filesystem::path &save,
                        const std::filesystem::path &destination);
}
