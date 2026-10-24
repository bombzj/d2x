#pragma once
#include <cstdint>
namespace d2x {struct SkillCastSpec;}
namespace d2x::server {struct PlayerState;}
namespace d2x::server::skills {
bool pd2StreamSkill(const SkillCastSpec &);
int32_t pd2BloodWarpCost(const PlayerState &,const SkillCastSpec &);
bool pd2SkillStartAllowed(const PlayerState &,const SkillCastSpec &);
bool pd2SkillEquipmentAllowed(const PlayerState &,const SkillCastSpec &);
}
