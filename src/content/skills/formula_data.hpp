#pragma once
#include <memory>
namespace d2x {
struct ClassicData;
struct SkillFormulaCatalog;
std::shared_ptr<const SkillFormulaCatalog> prepareSkillFormulaCatalog(const ClassicData &);
}
