#pragma once
namespace d2x {
class Archives;class AnimDataTable;struct ClassicData;struct MonsterRecord;struct HirelingDefinition;
namespace server {struct SkillRules;namespace companions {struct PreparedHireling;struct HirelingEquipment;}}
namespace hosting::pd2 {
void prepareHirelingSkills(const ClassicData &,server::SkillRules &);
void prepareHirelingActions(Archives &,const ClassicData &,const AnimDataTable &,
    const MonsterRecord &,const HirelingDefinition &,const server::companions::HirelingEquipment &,server::companions::PreparedHireling &);
}}
