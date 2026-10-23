#pragma once
#include <memory>
#include <span>
#include <cstdint>
namespace d2x {
struct ClassicData;
struct SkillFormulaCatalog;
std::shared_ptr<const SkillFormulaCatalog> prepareSkillFormulaCatalog(const ClassicData &, std::span<const uint8_t> missileRecords = {});
}
